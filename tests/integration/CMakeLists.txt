# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 AiMusic contributors

# 集成测试目录
# 约定：集成测试使用 aimusic_add_test 并显式指定 LABELS integration
# 示例：
# aimusic_add_test(
#     NAME integration_Example
#     SOURCES
#         tst_Example.cpp
#     LIBS
#         AiMusic::core
#     LABELS
#         integration
#     TIMEOUT
#         60
# )
