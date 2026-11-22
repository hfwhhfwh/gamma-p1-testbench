# Makefile — gamma-p1-testbench 顶层构建 / 一键回归   [GLOBREC-V1-B]
#
# 用途:
#   build   — 构建测试台 (等同 ./build.sh)
#   regress — 全量一键回归: 既有套件 (voice / fm B2+DX7 / modcov /
#             apatch / vco_c / route_hub / p0_modsrc) 与 GLOBREC 四目标
#             (globrec_test / globrec_test_trunc / stub_test /
#             globrec_fixture_test 守门) 并列; 任一目标失败即整体
#             回归失败 (非零退出码)。
#
# 变更摘要 (2026-09-09):
#   V1-B: 新建。GLOBREC 四目标正式编入全量回归。
#
# 变更摘要 (2026-10-25, TBENCH-INFRA-1):
#   S5 / P-40+P-67: regress-p0modsrc 原为"直接跑 ./build/p0_modsrc_test",
#   fresh build/ 下该手工产物不存在, 目标必败; 现改为先编译 (编译旗标
#   与 include 路径照抄 tests/build_tbfix3_tests.sh 的 p0_modsrc 段) 再
#   运行, 编译失败即目标失败。其余 regress 目标行为不变。
#
# 变更摘要 (2026-10-26, SNAPSHOT-FIX-1 / 测试台侧 WP3):
#   S6: P0M_KOBJS 追加 build/p0m_snapshot_v1.o (照脚本 CORE_OBJS 顺序,
#   位于 fm.o 与 kernel_capi.o 之间) 并照 :81-93 惯例补一条显式 cc 行;
#   注释"13 个内核对象"更新为 14。其余目标不动。

GP := ../gamma-p1

.PHONY: build regress \
        regress-voice regress-fm regress-modcov regress-apatch \
        regress-vco regress-hub regress-p0modsrc regress-globrec \
        globrec_test globrec_test_trunc stub_test globrec_fixture_test

build:
	./build.sh

# -------------------- GLOBREC 四目标 (构建并运行) --------------------
# 注: 二进制须在 tests/ 目录内运行 (夹具为相对路径 fixtures/)。

globrec_test globrec_test_trunc stub_test globrec_fixture_test:
	$(MAKE) -C $(GP)/tests $@
	cd $(GP)/tests && ./$@

# globrec_fixture_test 守门必须排在 globrec_test 之前运行:
# globrec_test 会确定性重写 fixtures/ (同字节), 守门须先读盘上既有
# 夹具, 才能捕获"旧 .act 逐位回放被破坏"的回归。
regress-globrec: globrec_fixture_test globrec_test globrec_test_trunc stub_test

# -------------------- 既有套件 (原样调用, 不动其 Makefile) ----------

regress-voice:
	$(MAKE) -C $(GP)/voice test

regress-fm:
	$(MAKE) -C $(GP)/fm test
	$(MAKE) -C $(GP)/fm test-dx7

regress-modcov:
	$(MAKE) -C $(GP)/modcov test

regress-apatch:
	$(MAKE) -C $(GP)/apatch test

regress-vco:
	$(MAKE) -C $(GP)/vco_c test

regress-hub:
	$(MAKE) -C $(GP)/route_hub run

# p0_modsrc_test 为手工编译产物 (惯例: 不进构建系统)。
# TBENCH-INFRA-1: P-40+P-67 fresh build/ 下产物不存在, 改为先编译再运行;
# 编译旗标与 include 路径照抄 tests/build_tbfix3_tests.sh 的 p0_modsrc 段。
# 产物命名加 p0m_ 前缀放 build/ 根, 避免与 build.sh 的 build/obj/*.o
# 同名冲突 (旗标不同: 此处 -Werror; 复用/混放会污染 build.sh 的增量判断)。
P0M_INC      := -Isrc -I$(GP) -I$(GP)/vco_c -I$(GP)/vcf_a -I$(GP)/vca \
                -I$(GP)/lfo -I$(GP)/env -I$(GP)/fm
P0M_CFLAGS   := -O2 -Wall -Wextra -Werror -std=c11 -pedantic -Wno-unused-parameter
P0M_CXXFLAGS := -O2 -Wall -Wextra -Werror -std=c++17 -Wno-unused-parameter
# 顺序照抄脚本 CORE_OBJS (15 个内核对象, 含 snapshot_v1.o /
# kernel_capi.o / action_log.o / platform_posix.o) [SNAPSHOT-FIX-1: 13 -> 14
# (内核 14 对象); TBENCH-WIN-1: 14 -> 15 (追加平台层对象, 脚本同名更新)]
P0M_KOBJS    := build/p0m_synth.o build/p0m_vmatrix.o build/p0m_voice.o \
                build/p0m_vco.o build/p0m_wavetables_data.o build/p0m_vcf.o \
                build/p0m_vca.o build/p0m_lfo.o build/p0m_lfo_wavetables_data.o \
                build/p0m_env.o build/p0m_fm.o build/p0m_snapshot_v1.o \
                build/p0m_kernel_capi.o build/p0m_action_log.o \
                build/p0m_platform_posix.o

regress-p0modsrc:
	mkdir -p build
	cc $(P0M_CFLAGS) $(P0M_INC) -c $(GP)/synth.c                  -o build/p0m_synth.o
	cc $(P0M_CFLAGS) $(P0M_INC) -c $(GP)/vmatrix.c                -o build/p0m_vmatrix.o
	cc $(P0M_CFLAGS) $(P0M_INC) -c $(GP)/voice.c                  -o build/p0m_voice.o
	cc $(P0M_CFLAGS) $(P0M_INC) -c $(GP)/vco_c/vco.c              -o build/p0m_vco.o
	cc $(P0M_CFLAGS) $(P0M_INC) -c $(GP)/vco_c/wavetables_data.c  -o build/p0m_wavetables_data.o
	cc $(P0M_CFLAGS) $(P0M_INC) -c $(GP)/vcf_a/vcf.c              -o build/p0m_vcf.o
	cc $(P0M_CFLAGS) $(P0M_INC) -c $(GP)/vca/vca.c                -o build/p0m_vca.o
	cc $(P0M_CFLAGS) $(P0M_INC) -c $(GP)/lfo/lfo.c                -o build/p0m_lfo.o
	cc $(P0M_CFLAGS) $(P0M_INC) -c $(GP)/lfo/lfo_wavetables_data.c -o build/p0m_lfo_wavetables_data.o
	cc $(P0M_CFLAGS) $(P0M_INC) -c $(GP)/env/env.c                -o build/p0m_env.o
	cc $(P0M_CFLAGS) $(P0M_INC) -c $(GP)/fm/fm.c                  -o build/p0m_fm.o
	cc $(P0M_CFLAGS) $(P0M_INC) -c $(GP)/snapshot_v1.c            -o build/p0m_snapshot_v1.o   # SNAPSHOT-FIX-1: S6
	cc $(P0M_CFLAGS) $(P0M_INC) -c src/kernel_capi.c              -o build/p0m_kernel_capi.o
	cc $(P0M_CFLAGS) $(P0M_INC) -c src/action_log.c               -o build/p0m_action_log.o
	cc $(P0M_CFLAGS) $(P0M_INC) -c src/platform_posix.c           -o build/p0m_platform_posix.o   # TBENCH-WIN-1
	c++ $(P0M_CXXFLAGS) $(P0M_INC) -c src/synth_ui.cpp            -o build/p0m_synth_ui.o
	c++ $(P0M_CXXFLAGS) $(P0M_INC) -c tests/p0_modsrc_test.cpp    -o build/p0m_p0_modsrc_test.o
	c++ -o build/p0_modsrc_test build/p0m_p0_modsrc_test.o $(P0M_KOBJS) \
	    build/p0m_synth_ui.o -lm
	./build/p0_modsrc_test

# -------------------- 全量一键回归 ----------------------------------

regress: regress-globrec regress-voice regress-fm regress-modcov \
         regress-apatch regress-vco regress-hub regress-p0modsrc
	@echo "== 全量回归: 全绿 =="
