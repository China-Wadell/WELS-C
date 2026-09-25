# WELS-C 顶层 Makefile — 编排各子项目
# 用 .RECIPEPREFIX 把配方前缀从 tab 换成 ">"

.RECIPEPREFIX = >

SUBDIRS = compiler runtime stdlib tools

.PHONY: all clean test $(SUBDIRS)

all: $(SUBDIRS)

compiler runtime stdlib tools:
> @if [ -f $@/Makefile ]; then \
>   echo "=== 编译 $@ ==="; \
>   $(MAKE) -C $@ || exit 1; \
> else \
>   echo "=== 跳过 $@（无 Makefile）==="; \
> fi

test:
> @if [ -f compiler/run_tests.sh ]; then \
>   echo "=== 测试 compiler ==="; \
>   (cd compiler && ./run_tests.sh); \
> fi

clean:
> @for d in $(SUBDIRS); do \
>   if [ -f $$d/Makefile ]; then \
>     echo "=== 清理 $$d ==="; \
>     $(MAKE) -C $$d clean; \
>   fi; \
> done
