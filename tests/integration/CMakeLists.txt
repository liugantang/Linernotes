# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

# 集成测试目录
# 约定：集成测试使用 linernotes_add_test 并显式指定 LABELS integration
# 示例：
# linernotes_add_test(
#     NAME integration_Example
#     SOURCES
#         tst_Example.cpp
#     LIBS
#         Linernotes::core
#     LABELS
#         integration
#     TIMEOUT
#         60
# )

add_subdirectory(app)
add_subdirectory(player)
