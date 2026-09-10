// MIT License
// Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.

#include <gtest/gtest.h>
#include <QApplication>
#include <QComboBox>
#include <QHeaderView>
#include <QScrollArea>
#include <QScrollBar>
#include <climits>
#include "code/codecolumns.h"
#include "code/instruction_style.h"
#include "code/isa_rows.h"

using Isa::InstructionKind;

TEST(InstructionStyle, ClassifiesRequestedPrefixesAndSpecificRulesFirst)
{
    const std::pair<const char*, InstructionKind> cases[] = {
        {"v_add_f32 v0, v1, v2",                       InstructionKind::Vector      },
        {" \tv_wmma_f32_16x16x16_f16 v[0:7], v[8:15]", InstructionKind::Matrix      },
        {"s_load_dwordx4 s[0:3], s[4:5], 0",           InstructionKind::ScalarMemory},
        {"s_waitcnt vmcnt(0)",                         InstructionKind::Wait        },
        {"s_wait_kmcnt 0",                             InstructionKind::Wait        },
        {"s_barrier",                                  InstructionKind::Wait        },
        {"s_add_u32 s0, s1, s2",                       InstructionKind::ScalarAlu   },
        {"s_addc_u32 s0, s1, s2",                      InstructionKind::ScalarAlu   },
        {"s_mul_i32",                                  InstructionKind::ScalarAlu   },
        {"s_sub_u32",                                  InstructionKind::ScalarAlu   },
        {"s_mov_b64",                                  InstructionKind::ScalarAlu   },
        {"s_movk_i32",                                 InstructionKind::ScalarAlu   },
        {"s_div_u32",                                  InstructionKind::ScalarAlu   },
        {"flat_load_dword",                            InstructionKind::Memory      },
        {"global_store_dword",                         InstructionKind::Memory      },
        {"buffer_load_dword",                          InstructionKind::Memory      },
        {"s_ttracedata",                               InstructionKind::Control     },
        {"s_endpgm",                                   InstructionKind::Control     },
        {"s_sendmsg sendmsg(MSG_INTERRUPT)",           InstructionKind::Control     },
        {"ds_read_b32",                                InstructionKind::Lds         },
        {"scratch_store_dword",                        InstructionKind::Scratch     },
        {"v_mfma_f32_16x16x4f32",                      InstructionKind::Matrix      },
        {"v_mfma_scale_f32_16x16x128_f8f6f4",          InstructionKind::Matrix      },
        {"s_and_saveexec_b64",                         InstructionKind::ScalarAlu   },
        {"s_lshl_b32",                                 InstructionKind::ScalarAlu   },
        {"s_cmp_eq_u32",                               InstructionKind::ScalarAlu   },
        {"s_cselect_b32",                              InstructionKind::ScalarAlu   },
        {"s_cvt_f32_i32",                              InstructionKind::ScalarAlu   },
        {"s_getpc_b64",                                InstructionKind::ScalarAlu   },
        {"s_get_pc_b64",                               InstructionKind::ScalarAlu   },
        {"s_setprio_inc 1",                            InstructionKind::ScalarAlu   },
        {"s_setprio 1",                                InstructionKind::Wait        },
        {"s_buffer_load_dword",                        InstructionKind::ScalarMemory},
        {"s_store_dword",                              InstructionKind::ScalarMemory},
        {"s_atomic_add",                               InstructionKind::ScalarMemory},
        {"s_atc_probe",                                InstructionKind::ScalarMemory},
        {"s_dcache_wb",                                InstructionKind::ScalarMemory},
        {"s_dcache_inv",                               InstructionKind::Wait        },
        {"s_memrealtime",                              InstructionKind::ScalarMemory},
        {"s_prefetch_inst",                            InstructionKind::ScalarMemory},
        {"s_inst_prefetch",                            InstructionKind::Wait        },
        {"s_wait_depctr 0",                            InstructionKind::Other       },
        {"s_wait_alu 0",                               InstructionKind::Other       },
        {"s_barrier_signal -1",                        InstructionKind::Wait        },
        {"s_delay_alu",                                InstructionKind::Other       },
        {"s_nop 0",                                    InstructionKind::Wait        },
        {"s_sleep 1",                                  InstructionKind::Wait        },
        {"s_clause 1",                                 InstructionKind::Wait        },
        {"s_ttracedata_imm 0",                         InstructionKind::Control     },
        {"s_trap 2",                                   InstructionKind::Control     },
        {"s_endpgm_saved",                             InstructionKind::Control     },
        {"s_sendmsg_rtn_b32",                          InstructionKind::Control     },
        {"s_scratch_load_dword",                       InstructionKind::Scratch     },
        {"tbuffer_load_format_x",                      InstructionKind::Memory      },
        {"image_sample",                               InstructionKind::Memory      },
        {"image_bvh_intersect_ray",                    InstructionKind::Memory      },
        {"cluster_load_b128",                          InstructionKind::Memory      },
        {"tensor_load_to_lds",                         InstructionKind::Memory      },
        {"dds_load_b32",                               InstructionKind::Memory      },
        {"lds_read_b32",                               InstructionKind::Lds         },
        {"ds_bvh_stack_rtn_b32",                       InstructionKind::Lds         },
        {"s_branch label_1",                           InstructionKind::Branch      },
        {"s_cbranch_scc0 label_1",                     InstructionKind::Branch      },
        {"s_call_b64 s[0:1], label_1",                 InstructionKind::Branch      },
        {"s_setpc_b64 s[0:1]",                         InstructionKind::Branch      },
        {"s_swappc_b64",                               InstructionKind::Branch      },
        {"s_set_pc_b64",                               InstructionKind::Branch      },
        {"s_swap_pc_b64",                              InstructionKind::Branch      },
        {"s_mov_b32 s0, branch_target",                InstructionKind::ScalarAlu   },
        {"v_mov_b32 v0, s0 ; branch",                  InstructionKind::Vector      },
        {"unknown_opcode branch",                      InstructionKind::Other       },
        {"; v_add_f32",                                InstructionKind::Other       },
        {"v_label:",                                   InstructionKind::Other       },
        {"",                                           InstructionKind::Other       },
        {"\t\n",                                       InstructionKind::Other       },
    };
    for (const auto& [text, kind] : cases)
    {
        SCOPED_TRACE(text);
        EXPECT_EQ(Isa::classifyInstruction(text), kind);
    }
}

TEST(InstructionStyle, SkippedAndUnknownInstructionsRemainUnstyled)
{
    for (const auto* instruction :
         {"s_wait_depctr 0",
          "s_wait_alu 0",
          "s_set_vgpr_count 1",
          "s_delay_alu 0",
          "buffer_nop",
          "buffer_unknown v0",
          "image_unknown v0",
          "unknown v0",
          ".long 0xffffffff"})
        EXPECT_EQ(Isa::classifyInstruction(instruction), InstructionKind::Other) << instruction;
}

TEST(InstructionStyle, ExtractsOnlyMnemonicAndLeavesOriginalStorageIntact)
{
    const std::string instruction = " \ts_add_f32\ts0, s1, s2 ; comment";
    const auto mnemonic = Isa::instructionMnemonic(instruction);
    EXPECT_EQ(mnemonic, "s_add_f32");
    EXPECT_EQ(mnemonic.data(), instruction.data() + 2);
    EXPECT_EQ(Isa::instructionMnemonic("s_endpgm"), "s_endpgm");
    EXPECT_EQ(Isa::instructionMnemonic("s_nop 0\r\n"), "s_nop");
    for (auto text : {"label_a:", "s_label:", "; _Z6kernelv", " ; comment", "// comment", " \t", ""})
        EXPECT_TRUE(Isa::instructionMnemonic(text).empty()) << text;
}

TEST(IsaRows, RecognizesDecoderAndAssemblyLabels)
{
    for (auto text : {"label_0001:", "\tlabel_0002", "; _Z6kernelv", " .LBB0_1:", "kernel:"})
        EXPECT_TRUE(Isa::isLabel(text)) << text;
    for (auto text : {"", "  ", "s_branch label_0001", "v_mov_b32 v0, 1", "; arbitrary comment"})
        EXPECT_FALSE(Isa::isLabel(text)) << text;
}

TEST(IsaRows, LabelsStartExpandedAndFoldingPreservesStableLineIndices)
{
    Isa::Rows rows;
    rows.reset({"preamble", "label_a:", "v_add", "s_waitcnt", "label_b:", "s_endpgm"});
    EXPECT_EQ(rows.count(), 6);
    EXPECT_TRUE(rows.foldingEnabled());
    EXPECT_FALSE(rows.sectionAt(1)->collapsed);
    rows.setFoldingEnabled(false);
    EXPECT_FALSE(rows.toggle(1));
    rows.setFoldingEnabled(true);
    EXPECT_EQ(rows.count(), 6);
    ASSERT_TRUE(rows.toggle(1));
    EXPECT_EQ(rows.count(), 4);
    EXPECT_EQ(rows.lineAt(0), 0);
    EXPECT_EQ(rows.lineAt(1), 1);
    EXPECT_EQ(rows.lineAt(2), 4);
    EXPECT_EQ(rows.rowOf(2), -1);
    EXPECT_EQ(rows.rowOf(4), 2);
    EXPECT_TRUE(rows.toggle(4));
    EXPECT_EQ(rows.count(), 3);
    rows.setFoldingEnabled(false);
    EXPECT_EQ(rows.count(), 6);
    rows.setFoldingEnabled(true);
    EXPECT_EQ(rows.count(), 6); // off clears the folds
}

TEST(IsaRows, RevealOnlyExpandsIntersectingSections)
{
    Isa::Rows rows;
    rows.reset({"label_a:", "v_add", "s_waitcnt", "label_b:", "s_endpgm"});
    rows.setFoldingEnabled(true);
    rows.toggle(0);
    rows.toggle(3);
    EXPECT_TRUE(rows.reveal(2, 2));
    EXPECT_EQ(rows.rowOf(2), 2);
    EXPECT_EQ(rows.rowOf(4), -1);
    EXPECT_FALSE(rows.reveal(2, 2));
    EXPECT_TRUE(rows.reveal(3, 3)); // a label jump also opens its section
    EXPECT_EQ(rows.count(), 5);
}

TEST(IsaRows, EmptyConsecutiveLabelsReloadAndInvalidIndices)
{
    Isa::Rows rows;
    rows.reset({});
    EXPECT_EQ(rows.count(), 0);
    EXPECT_EQ(rows.lineAt(-1), -1);
    EXPECT_EQ(rows.rowOf(0), -1);
    rows.reset({"label_a:", "label_b:", "v_add", "label_c:"});
    rows.setFoldingEnabled(true);
    EXPECT_FALSE(rows.toggle(0));
    EXPECT_FALSE(rows.toggle(3));
    EXPECT_FALSE(rows.toggle(-1));
    EXPECT_FALSE(rows.toggle(20));
    EXPECT_TRUE(rows.toggle(1));
    rows.expandAll();
    EXPECT_EQ(rows.count(), 4);
    rows.toggle(1);
    rows.reset({"v_add", "s_endpgm"});
    EXPECT_EQ(rows.count(), 2);
    EXPECT_EQ(rows.sectionAt(0), nullptr);
}

TEST(IsaRows, VisibleRangeIsBoundedByViewportNotListingSize)
{
    Isa::Rows rows;
    std::vector<std::string_view> lines(25000, "v_add_f32 v0, v1, v2");
    lines[100] = "label_a:";
    lines[24000] = "label_b:";
    rows.reset(lines);
    EXPECT_EQ(rows.visibleRange(24000 * 20 + 7, 400, 20), std::make_pair(24000, 24021));
    EXPECT_EQ(rows.visibleRange(0, 400, 20), std::make_pair(0, 20));
    EXPECT_EQ(rows.visibleRange(0, 0, 20), std::make_pair(0, 0));
    EXPECT_EQ(rows.visibleRange(0, 400, 0), std::make_pair(0, 0));
    EXPECT_EQ(rows.visibleRange(INT_MAX, INT_MAX, 20), std::make_pair(25000, 25000));
    rows.setFoldingEnabled(true);
    rows.toggle(100);
    EXPECT_EQ(rows.lineAt(101), 24000);
    EXPECT_EQ(rows.rowOf(24000), 101);
    EXPECT_EQ(rows.visibleRange(2000, 400, 20), std::make_pair(100, 120));
}

TEST(CodeColumns, ResizesBodyAndHeaderTogetherAndRestoresHiddenWidths)
{
    CodeColumns columns;
    auto* instruction = new QWidget();
    auto* costs = new QWidget();
    auto* combo = new QComboBox();
    combo->addItem("Latency: Sum all");
    columns.addColumn("Instruction", instruction);
    columns.addColumn("Latency", costs, combo);
    columns.setColumnWidth(0, 320);
    columns.setColumnWidth(1, 150);
    columns.resize(600, 400);
    columns.show();
    QApplication::processEvents();
    EXPECT_EQ(instruction->width(), 320);
    EXPECT_EQ(costs->x(), 320);
    EXPECT_EQ(costs->width(), 150);
    EXPECT_EQ(instruction->height(), costs->height());
    EXPECT_EQ(columns.minimumWidth(), 470);
    columns.setColumnVisible(1, false);
    EXPECT_TRUE(costs->isHidden());
    EXPECT_TRUE(combo->isHidden());
    EXPECT_EQ(columns.minimumWidth(), 320);
    columns.setColumnVisible(1, true);
    EXPECT_EQ(costs->width(), 150);
    EXPECT_FALSE(combo->isHidden());
    columns.setColumnWidth(0, 60);
    EXPECT_EQ(costs->x(), 60);
    EXPECT_EQ(instruction->width(), 60);
    columns.setColumnWidth(0, 1);
    EXPECT_EQ(instruction->width(), 48);
    columns.setColumnWidth(0, INT_MAX);
    EXPECT_EQ(instruction->width(), 4096);
}

TEST(CodeColumns, OverflowUsesEnclosingHorizontalScrollbar)
{
    QScrollArea area;
    area.setWidgetResizable(true);
    area.setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* columns = new CodeColumns();
    columns->addColumn("Instruction", new QWidget());
    columns->addColumn("Address", new QWidget());
    area.setWidget(columns);
    area.resize(400, 200);
    area.show();
    columns->setColumnWidth(0, 700);
    QApplication::processEvents();
    EXPECT_GT(area.horizontalScrollBar()->maximum(), 0);
    columns->setColumnWidth(0, 100);
    columns->setColumnWidth(1, 100);
    QApplication::processEvents();
    EXPECT_EQ(area.horizontalScrollBar()->maximum(), 0);
}
