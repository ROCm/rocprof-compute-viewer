# Configuration file for the Sphinx documentation builder.
#
# This file only contains a selection of the most common options. For a full
# list see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html

import re

'''
html_theme is usually unchanged (rocm_docs_theme).
flavor defines the site header display, select the flavor for the corresponding portals
flavor options: rocm, rocm-docs-home, rocm-blogs, rocm-ds, instinct, ai-developer-hub, local, generic
'''
html_theme = "rocm_docs_theme"
html_theme_options = {"flavor": "rocm-docs-home"}


# This section turns on/off article info
setting_all_article_info = True
all_article_info_os = ["linux"]
all_article_info_author = ""

# Dynamically extract component version
with open('../CMakeLists.txt', encoding='utf-8') as f:
    pattern = r'.*\brocprof-compute-viewer\s+VERSION\s+([0-9.]+)' # Update according to each component's CMakeLists.txt
    match = re.search(pattern,
                      f.read())
    if not match:
        raise ValueError("VERSION not found!")
    version_number = match[1]

# for PDF output on Read the Docs
project = "rocprof-compute-viewer"
author = "Advanced Micro Devices, Inc."
copyright = "Copyright (c) 2025 Advanced Micro Devices, Inc. All rights reserved."
version = version_number
release = version_number

external_toc_path = "./sphinx/_toc.yml" # Defines Table of Content structure definition path

# Add more addtional package accordingly
extensions = [
    "rocm_docs", 
] 

html_title = f"{project} {version_number} documentation"

external_projects_current_project = "rocprof-compute-viewer"
