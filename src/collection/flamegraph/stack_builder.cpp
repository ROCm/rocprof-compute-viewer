// MIT License
//
// Copyright (c) 2024-2026 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "stack_builder.h"

#include <algorithm>
#include <iostream>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "code/asmcode.h"
#include "code/sourcefile.h"
#include "data/datastore.h"
#include "data/marker_colors.h"
#include "data/records.h"
#include "data/shaderdata.h"
#include "data/wavemanager.h"
#include "mainwindow.h"

namespace flamegraph
{

namespace
{

constexpr const char* kUnassignedFile = "[Unassigned]";
constexpr const char* kNoScope = "[no scope]";

std::string trimLeadingWhitespace(const std::string& str)
{
    size_t start = str.find_first_not_of(" \t");
    return (start != std::string::npos) ? str.substr(start) : str;
}

std::string basename(const std::string& path)
{
    auto pos = path.find_last_of('/');
    return (pos != std::string::npos) ? path.substr(pos + 1) : path;
}

/// Stable secondary sort on depth ascending — when two spans share the same
/// enter_time, the parent must come before the child or stack walks see the
/// child below the parent and attribute the wrong scope.
///
/// ResolveMarkers only sorts by enter_time; this restores parent-before-child
/// order without disturbing the original sequence on distinct enter_times.
/// Used by both the integrated builder (per-slot) and the global marker
/// builder (per-bucket).
void sortMarkerSpansForWalk(std::vector<const MarkerSpan*>& spans)
{
    std::stable_sort(
        spans.begin(),
        spans.end(),
        [](const MarkerSpan* a, const MarkerSpan* b)
        { return std::tie(a->enter_time, a->depth) < std::tie(b->enter_time, b->depth); }
    );
}

/// Canonical key for a marker span used everywhere we collapse identical
/// scopes into one StackNode: "<kind>:<name>" with "@id<n>" appended when
/// the name is unresolved so distinct unresolved IDs don't collide.
std::string markerScopeKey(const MarkerSpan& s)
{
    std::string key = std::to_string(static_cast<int>(s.kind)) + ":" + s.name;
    if (s.name.empty()) key += "@id" + std::to_string(s.marker_id);
    return key;
}

/// Look up — or insert — the StackNode in `slot_map` for marker span `s`.
/// `slot_map` is either the top-level Roots map (root-level markers) or
/// some node's `children` (nested markers).
StackNode* getOrCreateMarkerNode(std::map<std::string, std::shared_ptr<StackNode>>& slot_map, const MarkerSpan& s)
{
    std::string key = markerScopeKey(s);
    auto& slot = slot_map[key];
    if (!slot)
    {
        slot = std::make_shared<StackNode>();
        slot->key = std::move(key);
        slot->label = s.name.empty() ? std::string("(unresolved)") : s.name;
        slot->fullLocation = s.source_loc;
        slot->isMarker = true;
        slot->markerKind = s.kind;
        slot->markerSourceLoc = s.source_loc;
        slot->markerColor = MarkerColor(s.kind, s.name);
    }
    return slot.get();
}

/// Result of inspecting an ASMLine for its innermost source file. `isUnassigned`
/// flags the [Unassigned] fallback bucket; `hasResolvedRef` is true when the
/// inline-call chain in line_ref resolved to real source (vs the parsed-from-
/// SOURCEREF-text fallback).
struct InnerFile
{
    std::string filename;
    bool isUnassigned = false;
    bool hasResolvedRef = false;
};

InnerFile pickInnerFile(ASMCodeline* asmLine, const ASMLine& asmElem)
{
    InnerFile out;
    const auto& refs = asmElem.line_ref;

    if (!refs.empty())
        if (auto r = refs.back().lock(); r && r->parent) out.filename = r->parent->filename;

    if (out.filename.empty())
    {
        std::string_view src = asmLine->elements.at(ASMCodeline::Element::ESOURCEREF)->getStdText();
        if (auto a = src.rfind(" -> "); a != std::string_view::npos) src.remove_prefix(a + 4);
        auto colon = src.rfind(':');
        if (colon != std::string_view::npos && colon > 0)
            out.filename = std::string(src.substr(0, colon));
        else
            out.filename = kUnassignedFile;
    }

    out.hasResolvedRef = !refs.empty() && refs.back().lock() && refs.back().lock()->parent;
    out.isUnassigned = (out.filename == kUnassignedFile);
    return out;
}

StackNode* getOrCreateFileNode(StackNode* parent, const std::string& filename, bool isUnassigned)
{
    auto& fileNode = parent->children[filename];
    if (!fileNode)
    {
        fileNode = std::make_shared<StackNode>();
        fileNode->key = filename;
        fileNode->label = isUnassigned ? filename : basename(filename);
        fileNode->filename = isUnassigned ? std::string() : filename;
    }
    return fileNode.get();
}

/// Build the file → inlined-source-line chain beneath fileNode and return the
/// leaf node (innermost source line, where asm entries land). When the asm
/// line has no resolved DWARF refs, emits a single ":?" placeholder line.
StackNode* appendInlineChain(StackNode* fileNode, const ASMLine& asmElem, const InnerFile& inner, int64_t lat)
{
    const auto& refs = asmElem.line_ref;
    StackNode* current = fileNode;

    if (inner.hasResolvedRef)
    {
        for (size_t i = refs.size(); i-- > 0;)
        {
            auto locked = refs[i].lock();
            if (!locked) continue;

            std::string srcFile = locked->parent ? locked->parent->filename : "Unknown";
            int displayLine = locked->line_number + 1;
            std::string key = srcFile + ":" + std::to_string(locked->line_number);
            std::string location = basename(srcFile) + ":" + std::to_string(displayLine);

            auto& child = current->children[key];
            if (!child)
            {
                child = std::make_shared<StackNode>();
                child->key = key;
                child->label = location;
                child->content = trimLeadingWhitespace(locked->getStdText());
                child->fullLocation = srcFile + ":" + std::to_string(displayLine);
                child->filename = srcFile;
                child->lineNumber = locked->line_number;
            }
            child->latency += lat;
            current = child.get();
        }
    }
    else
    {
        std::string key = inner.filename + ":?";
        auto& child = current->children[key];
        if (!child)
        {
            child = std::make_shared<StackNode>();
            child->key = key;
            child->label = (inner.isUnassigned ? inner.filename : basename(inner.filename)) + ":?";
            child->fullLocation = inner.filename + ":?";
            child->filename = inner.isUnassigned ? std::string() : inner.filename;
            child->lineNumber = -1;
        }
        child->latency += lat;
        current = child.get();
    }

    return current;
}

/// Append (and merge-by-label) one asm-instruction entry under `leaf`.
/// In the integrated path the same ASMCodeline can be added many times
/// (once per execution that lands inside the marker), so collapsing by label
/// avoids one frame per execution.
void mergeAsmEntry(StackNode* leaf, ASMCodeline* asmLine, const ASMLine& asmElem, int64_t lat)
{
    StackNode::AsmEntry entry;
    entry.label = trimLeadingWhitespace(asmElem.getStdText());
    entry.latency = lat;
    entry.asmIndex = asmLine->line_index;

    int bestType = 0;
    int64_t bestLat = 0;
    for (size_t t = 0; t < asmLine->hotspot.typed_latency.size(); t++)
    {
        if (asmLine->hotspot.typed_latency[t] > bestLat)
        {
            bestLat = asmLine->hotspot.typed_latency[t];
            bestType = static_cast<int>(t);
        }
    }
    entry.tokenType = bestType;

    auto [it, inserted] = leaf->asmIndexByLabel.try_emplace(entry.label, leaf->asmEntries.size());
    if (inserted)
        leaf->asmEntries.push_back(std::move(entry));
    else
        leaf->asmEntries[it->second].latency += entry.latency;
}

/// Add a single ASM-instruction contribution into a parent node so that the
/// file → inlined-source-line → asm subtree is grown beneath it. Increments
/// fileNode latency on the way; creates fileNode under `parent` if it does
/// not yet exist. `lat` is added to every node on the way to the leaf.
void addAsmContribution(StackNode* parent, ASMCodeline* asmLine, int64_t lat)
{
    if (!parent || !asmLine || lat <= 0) return;

    auto* asmElem = dynamic_cast<ASMLine*>(asmLine->elements.at(ASMCodeline::Element::EASM).get());
    if (!asmElem) return;

    InnerFile inner = pickInnerFile(asmLine, *asmElem);
    StackNode* fileNode = getOrCreateFileNode(parent, inner.filename, inner.isUnassigned);
    fileNode->latency += lat;

    StackNode* leaf = appendInlineChain(fileNode, *asmElem, inner, lat);

    // Skip per-instruction breakdown for unassigned PCs: every PC the trace
    // touched ends up here when it has no DWARF source mapping, which on a
    // typical multi-kernel trace is tens of thousands of unique disassembled
    // strings — each producing one Frame and bloating output enough to make
    // every repaint slow. The cumulative latency is still visible as the
    // "[Unassigned]:?" bar; there's no source row to drill into anyway.
    if (inner.isUnassigned) return;

    mergeAsmEntry(leaf, asmLine, *asmElem, lat);
}

} // anonymous namespace

Roots buildSourceRoots()
{
    // Single virtual parent across every asm line so addAsmContribution reuses
    // the live file / inlined-source-line / asm nodes via map[key] lookups.
    StackNode virtualRoot;

    for (auto& asmLine : ASMCodeline::line_vec)
    {
        if (!asmLine) continue;
        int64_t lat = asmLine->hotspot.combined();
        if (lat <= 0) continue;
        addAsmContribution(&virtualRoot, asmLine.get(), lat);
    }

    return std::move(virtualRoot.children);
}

Roots buildIntegratedRoots(int t_se, int t_cu, int t_simd)
{
    Roots roots;

    if (!MainWindow::window) return roots;
    auto* mw = MainWindow::window;
    auto* mgr = mw->shaderdata_manager;
    auto& store = mw->data_store;
    if (!mgr || !mgr->HasMarkers() || !store) return roots;

    // (codeobj_id, address) → ASMCodeline*. Built once per call. Skipping JSON
    // path is implicit: pc.{address,code_object_id} are zero on JSON instructions
    // so they never lookup-match a real asm line.
    //
    // Use a real composite key — bit-packing two 64-bit values into one would
    // alias on traces with many code objects sharing virtual addresses.
    struct PCKey
    {
        uint64_t coid;
        uint64_t addr;
        bool operator==(const PCKey& o) const noexcept { return coid == o.coid && addr == o.addr; }
    };
    struct PCKeyHash
    {
        size_t operator()(const PCKey& k) const noexcept
        {
            return std::hash<uint64_t>{}(k.coid) ^ (std::hash<uint64_t>{}(k.addr) * 0x9E3779B97F4A7C15ull);
        }
    };
    std::unordered_map<PCKey, ASMCodeline*, PCKeyHash> asm_by_pc;
    asm_by_pc.reserve(ASMCodeline::line_vec.size() * 2);
    std::unordered_map<int, ASMCodeline*> asm_by_line;
    asm_by_line.reserve(ASMCodeline::line_map.size());
    for (auto& asmLine : ASMCodeline::line_vec)
    {
        if (!asmLine) continue;
        asm_by_line.emplace(asmLine->line_number, asmLine.get());
        auto* asmElem = dynamic_cast<ASMLine*>(asmLine->elements.at(ASMCodeline::Element::EASM).get());
        if (!asmElem) continue;
        if (asmElem->addr == 0 && asmElem->codeobj == 0) continue;
        asm_by_pc.emplace(
            PCKey{static_cast<uint64_t>(asmElem->codeobj), static_cast<uint64_t>(asmElem->addr)}, asmLine.get()
        );
    }
    if (asm_by_pc.empty() && asm_by_line.empty()) return roots;

    auto& wave_hierarchy = store->wave_hierarchy;

    auto se_it = wave_hierarchy.find(t_se);
    if (se_it == wave_hierarchy.end()) return roots;
    auto simd_it = se_it->second.find(t_simd);
    if (simd_it == se_it->second.end()) return roots;

    // Bucket per-instruction contributions by (leaf, asmLine) so addAsmContribution
    // runs once per unique pair instead of once per wave instruction. With every
    // PC now disassembled (post-merged-ISA-cache fix), a single SIMD slot can
    // hold millions of wave instructions but only ~10^4 distinct PCs and a small
    // number of marker leaves — so this collapses millions of std::map traversals
    // into ~10^4-10^5 calls.
    struct LeafAsmKey
    {
        StackNode* leaf;
        ASMCodeline* asmLine;
        bool operator==(const LeafAsmKey& o) const { return leaf == o.leaf && asmLine == o.asmLine; }
    };
    struct LeafAsmHash
    {
        size_t operator()(const LeafAsmKey& k) const noexcept
        {
            return std::hash<void*>{}(k.leaf) ^ (std::hash<void*>{}(k.asmLine) * 0x9E3779B97F4A7C15ull);
        }
    };
    std::unordered_map<LeafAsmKey, int64_t, LeafAsmHash> contrib;
    contrib.reserve(std::max(asm_by_pc.size(), asm_by_line.size()) * 4);

    struct SlotInstruction
    {
        int64_t time = 0;
        int64_t duration = 0;
        ASMCodeline* asmLine = nullptr;
    };

    for (const auto& [slot, instance_map] : simd_it->second)
    {
        // Collect this slot's instructions (only those on the target CU). The
        // decoder path keeps wave_record_t in memory; the pure JSON path loads
        // WaveInstance lazily and maps token.code_line back to ASMCodeline.
        std::vector<SlotInstruction> slot_instructions;
        for (const auto& [_id, entry] : instance_map)
        {
            bool loaded_from_record = false;
            {
                std::shared_lock<std::shared_mutex> wr_lock(store->wave_records_mutex);
                auto wr_it = store->wave_records.find(entry.id);
                if (wr_it != store->wave_records.end())
                {
                    loaded_from_record = true;
                    if (wr_it->second.cu == t_cu)
                    {
                        slot_instructions.reserve(slot_instructions.size() + wr_it->second.instructions.size());
                        for (const auto& inst : wr_it->second.instructions)
                        {
                            auto asm_it = asm_by_pc.find(PCKey{inst.pc.code_object_id, inst.pc.address});
                            if (asm_it == asm_by_pc.end()) continue;
                            slot_instructions.push_back({inst.time, std::max<int64_t>(1, inst.duration), asm_it->second}
                            );
                        }
                    }
                }
            }
            if (loaded_from_record) continue;

            std::shared_ptr<WaveInstance> wave;
            try
            {
                wave = store->getWave(entry);
            }
            catch (const std::exception& e)
            {
                std::cerr << "flamegraph: failed to load wave " << entry.id << ": " << e.what() << "\n";
                continue;
            }
            catch (...)
            {
                std::cerr << "flamegraph: failed to load wave " << entry.id << "\n";
                continue;
            }
            if (!wave) continue;
            if (wave->cu >= 0 && wave->cu != t_cu) continue;

            slot_instructions.reserve(slot_instructions.size() + wave->tokens.size());
            for (const auto& token : wave->tokens)
            {
                auto asm_it = asm_by_line.find(token.code_line);
                if (asm_it == asm_by_line.end()) continue;
                slot_instructions.push_back({token.clock, std::max<int64_t>(1, token.cycles), asm_it->second});
            }
        }
        if (slot_instructions.empty()) continue;

        std::sort(
            slot_instructions.begin(),
            slot_instructions.end(),
            [](const SlotInstruction& a, const SlotInstruction& b) { return a.time < b.time; }
        );

        // Slot's marker spans (already sorted by enter_time in ResolveMarkers).
        auto spans = mgr->GetMarkers(t_se, t_cu, t_simd, slot);

        std::vector<const MarkerSpan*> span_ptrs;
        if (spans)
        {
            span_ptrs.reserve(spans->size());
            for (const auto& s : *spans) span_ptrs.push_back(&s);
            sortMarkerSpansForWalk(span_ptrs);
        }

        // Span pointer-stack of currently-open scopes at the cursor's time.
        std::vector<const MarkerSpan*> open_stack;
        size_t span_idx = 0;

        for (const auto& inst : slot_instructions)
        {
            // Pop scopes that closed before this instruction.
            while (!open_stack.empty() && !open_stack.back()->is_open && open_stack.back()->exit_time <= inst.time)
            {
                open_stack.pop_back();
            }
            // Push scopes that have entered by now (skip points — instantaneous).
            while (span_idx < span_ptrs.size() && span_ptrs[span_idx]->enter_time <= inst.time)
            {
                const MarkerSpan& s = *span_ptrs[span_idx++];
                if (s.is_point) continue;
                if (!s.is_open && s.exit_time <= inst.time) continue;
                // Pop any tail entries this span supplants (defensive — should be
                // unnecessary because spans nest, but guards against malformed data).
                while (!open_stack.empty() && !open_stack.back()->is_open &&
                       open_stack.back()->exit_time <= s.enter_time)
                {
                    open_stack.pop_back();
                }
                open_stack.push_back(&s);
            }

            ASMCodeline* asmLine = inst.asmLine;
            if (!asmLine) continue;
            int64_t lat = inst.duration;

            // Walk the marker stack: outermost is root in `roots`, inner ones are
            // children. The leaf marker (or "[no scope]" root) hosts the file/line/asm.
            StackNode* leaf = nullptr;
            if (open_stack.empty())
            {
                auto& slot_node = roots[kNoScope];
                if (!slot_node)
                {
                    slot_node = std::make_shared<StackNode>();
                    slot_node->key = kNoScope;
                    slot_node->label = kNoScope;
                }
                leaf = slot_node.get();
                leaf->latency += lat;
            }
            else
            {
                StackNode* current = getOrCreateMarkerNode(roots, *open_stack[0]);
                current->latency += lat;
                for (size_t i = 1; i < open_stack.size(); ++i)
                {
                    current = getOrCreateMarkerNode(current->children, *open_stack[i]);
                    current->latency += lat;
                }
                leaf = current;
            }

            contrib[{leaf, asmLine}] += lat;
        }
    }

    for (const auto& [k, lat] : contrib) addAsmContribution(k.leaf, k.asmLine, lat);

    return roots;
}

Roots buildGlobalMarkerRoots()
{
    Roots roots;

    if (!MainWindow::window) return roots;
    auto* mgr = MainWindow::window->shaderdata_manager;
    if (!mgr || !mgr->HasMarkers()) return roots;

    // Build a single global containment tree by walking every non-empty bucket.
    // Nodes are merged across buckets by (kind, name) so identical scopes
    // collapse into one frame whose width sums durations from every wave/slot.
    mgr->ForEachMarkerBucket(
        [&](int /*se*/, int /*cu*/, int /*simd*/, int /*slot*/, const MarkerSpanVec& spans)
        {
            if (!spans || spans->empty()) return;

            std::vector<const MarkerSpan*> ordered;
            ordered.reserve(spans->size());
            for (const auto& s : *spans) ordered.push_back(&s);
            sortMarkerSpansForWalk(ordered);

            std::vector<StackNode*> stack; // stack[0] is the matched root
            stack.reserve(8);

            for (const auto* sp : ordered)
            {
                const auto& s = *sp;
                // Points are instantaneous events, not scopes — skip them entirely
                // (matches the detail flamegraph's behavior at the per-slot walk above).
                if (s.is_point) continue;
                // Same trim-by-depth strategy as the detail flamegraph.
                while (static_cast<int>(stack.size()) > s.depth) stack.pop_back();

                auto& slot_map = stack.empty() ? roots : stack.back()->children;
                StackNode* node = getOrCreateMarkerNode(slot_map, s);

                int64_t dur = s.is_open ? 1 // Open spans across all waves don't have a clean clamp; minimal weight.
                                        : std::max<int64_t>(1, s.exit_time - s.enter_time);

                node->latency += dur;
                stack.push_back(node);
            }
        }
    );

    return roots;
}

} // namespace flamegraph
