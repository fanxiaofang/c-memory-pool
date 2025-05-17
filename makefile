
# 编译器设置
CC := gcc

# 配置选项
DEBUG ?= 0
STD := c11

BASE_CFLAGS := -std=$(STD) -Wall -I. -pthread
# 编译选项设置
# -Wall 启用编译警告信息
# -I 指定头文件搜索路径
# -pthread 启用多线程支持
# -g 生成调试信息,为调试器（如GDB）提供额外信息, 仅开发调试
# -fsanitize=address 内存错误检测工具 AddressSanitizer， 仅开发调试

# 调试选项
ifeq ($(DEBUG),1)
	#调试模式：启用sanitizer
	SANITIZE_FLAGS = -fsanitize=address -g
	CFLAGS := $(BASE_CFLAGS) $(SANITIZE_FLAGS)
	LDFLAGS := $(SANITIZE_FLAGS) -pthread
else
	#普通发布模式：禁用sanitizer
	CFLAGS := $(BASE_CFLAGS) -g
	LDFLAGS := -pthread
endif

# 源文件和目标文件处理
SRC_DIR := .
SRCS := $(wildcard $(SRC_DIR)/*.c) # 查找所有.c文件
OBJS := $(SRCS:.c=.o) # 将.c文件转为.o
TARGET := memory_pool_test # 最终可执行文件名

# 默认目标 ：生成最终可执行文件
all: $(TARGET)

# 链接目标文件（.o）生成可执行文件
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
# $^ 所有依赖文件，即$(TARGET):右侧的所有依赖文件，这里是所有.文件
# $@ 目标文件名 这里是$(TATGET)

# 通用编译规则,编译.c为.o
%.o: %.c memoryPool.h
	$(CC) $(CFLAGS) -c $< -o $@
# -c 只编译不链接
# $< 第一个依赖文件（.c)
# $@ 目标文件 (.o)
# 依赖头文件memoryPool.c的作用：依赖追踪 如果memoryPool.c发生改变 则依赖这头文件的所有.o 都会重新编译

# 清理规则
# 清理中间文件和可执行文件
clean:
	rm -f $(OBJS) $(TARGET)

help:
	@echo "使用方法"
	@echo "	make"			# 生成普通版本
	@echo "	make DEBUG=1"	# 生成带 Sanitizer内存检测工具的调试版本
	@echo "	make clean"		# 清理编译文件

# 声明伪目标，防止与文件名冲突
.PHONY: all clean
