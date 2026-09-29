# Cache Lab 实验复盘
# Part A Cache Simulator

---

## 0. 本次实验的目标

Cache Lab 的 Part A 不是让程序去修改真实 CPU Cache，也不是让程序保存真实内存数据，而是实现一个**软件 Cache Simulator**：

```text
读命令行参数
        ↓
根据 s、E、b 创建空Cache
        ↓
打开 trace 文件
        ↓
逐行读取内存访问
        ↓
忽略 I
        ↓
处理 L、S、M
        ↓
从地址中计算 set_index 和 tag
        ↓
在对应的 set 里查找
        ↓
判断 hit/miss/eviction
        ↓
更新 LRU 状态
        ↓
输出总统计结果
```

Part A 的核心可以抽象成一个状态转移模型：

```text
当前 Cache 状态 + 下一次数据访问
                ↓
       新的 Cache 状态 + 统计量变化
```

其中 Cache 状态主要由每条 line 的以下元数据组成：

```text
valid：当前 line 是否有有效 block
 tag：当前 line 保存的 block 标识
 lru：最近使用顺序
```

Part A 不需要保存 block 中实际的字节内容，因为评分只关心 Cache 的行为统计，不关心读取出的数据值。

---

# 模块 A：全流程解题思路、推导和实现过程

## 1. 阅读官方说明后确认的约束

官方 PDF 明确了 Part A 的输入格式、模拟范围和评分要求。

### 1.1 只模拟数据 Cache

Valgrind trace 中有四种操作：

```text
I：instruction load，指令读取
L：data load，数据读取
S：data store，数据写入
M：data modify，数据修改
```

本实验只模拟数据 Cache，因此：

```text
I 必须忽略
L、S、M 才进入 Cache 模拟
```

忽略 `I` 的含义不是让 `fscanf` 停止，而是：

1. 正常读出这条记录；
2. 判断操作字符是 `I`；
3. 使用 `continue` 跳过后续 Cache 处理；
4. 不修改 hit、miss、eviction 和 LRU 状态。

### 1.2 `L`、`S`、`M` 的访问次数

官方规定：

```text
L：一次 Cache 访问
S：一次 Cache 访问
M：一次 Load 加一次 Store，也就是两次 Cache 访问
```

因此：

```text
M address,size
```

应该理解为：

```text
第一次访问 address：模拟 Load
第二次访问 address：模拟 Store
```

两次访问之间没有其他 trace 记录，所以第一次访问完成后，第二次同地址访问通常会命中。

### 1.3 忽略访问长度

官方假设一次访问已经正确对齐，不会跨越 block boundary。因此 trace 中：

```text
L 10,8
```

虽然 `8` 表示访问长度，但本实验中只需要用它完成格式解析，不需要用它拆成多个 Cache 访问。

### 1.4 Cache 参数

命令行参数形式为：

```text
-s <s>：set index 位数
-E <E>：每个 set 中的 line 数
-b <b>：block offset 位数
-t <file>：trace 文件路径
-v：verbose 输出
-h：帮助
```

由参数得到：

```text
S = 2^s：set 数量
E       ：每个 set 的 line 数
B = 2^b：每个 block 的字节数
```

例如：

```text
s = 2, E = 1, b = 3
```

表示：

```text
4 个 set
每个 set 1 条 line
每个 block 8 字节
```

### 1.5 动态分配要求

官方要求程序能够支持任意合法的 `s`、`E`、`b`，所以不能写死：

```text
固定 4 个 set
固定每组 1 条 line
固定 block 为 8 字节
```

必须根据参数动态分配 Cache 的数据结构。

### 1.6 LRU 要求

参考模拟器使用 LRU（Least Recently Used）策略：

```text
当目标 set 已满并且访问未命中时，驱逐最长时间没有被访问的 line
```

---

## 2. 环境准备和目录检查

### 2.1 检查 `04_CacheLab` 文件

当前目录中确认存在：

```text
README
Makefile
csim.c
trans.c
cachelab.c
cachelab.h
test-csim
csim-ref
test-trans
tracegen
driver.py
traces/
```

`README` 明确说明真正需要修改的文件只有：

```text
csim.c
trans.c
```

Part A 主要使用：

```text
csim.c       ：学生实现
csim-ref     ：参考模拟器
test-csim    ：自动测试
traces/      ：输入 trace
cachelab.c  ：提供 printSummary
```

### 2.2 Valgrind 安装过程中的问题

第一次检查时执行：

```bash
valgrind --version
```

系统提示 Valgrind 尚未安装。随后执行：

```bash
sudo apt update && sudo apt install -y valgrind
```

APT 卡在：

```text
[已连接到 archive.ubuntu.com] [正在等待报头]
```

这说明问题发生在软件源连接之后、HTTP 响应头返回之前，可能与镜像、网络路由、IPv6、代理或 WSL DNS 有关。此时建议先用 `Ctrl+C` 安全中断，不要反复无期限等待。

之后 Valgrind 安装成功，验证结果为：

```text
valgrind-3.26.0
```

Part A 本身不依赖 Valgrind；Valgrind 主要在 Part B 的 `test-trans` 中用于生成内存访问轨迹。

### 2.3 首次重新构建

执行：

```bash
make clean
make
```

Makefile 使用的编译参数为：

```text
-g -Wall -Werror -std=c99 -m64
```

含义：

- `-g`：生成调试信息；
- `-Wall`：开启常见警告；
- `-Werror`：警告视为错误；
- `-std=c99`：使用 C99；
- `-m64`：生成 64 位目标。

---

## 3. 空程序基线：先确认问题在哪里

最开始的 `csim.c` 只有占位逻辑，因此运行：

```bash
./csim -s 2 -E 1 -b 3 -t traces/trans.trace
```

得到：

```text
hits:0 misses:0 evictions:0
```

相同配置下运行参考程序：

```bash
./csim-ref -s 2 -E 1 -b 3 -t traces/trans.trace
```

得到：

```text
hits:167 misses:71 evictions:67
```

由此确认：

```text
编译环境正常
trace 文件路径正常
csim-ref 可用
学生程序还没有读取或模拟任何访问
```

这里没有把“输出三个零”误认为测试通过，而是把它当作实现前基线。

---

## 4. 先用小例子理解 hit、miss、eviction

在正式修改代码前，使用官方模型做了一个手算练习：

```text
s = 1, E = 1, b = 1
```

这表示：

```text
2 个 set
每个 set 1 条 line
每个 block 2 字节
```

观察 `yi2.trace` 的前几条：

```text
L 0,1
L 1,1
L 2,1
L 3,1
S 4,1
L 5,1
S 6,1
```

### 4.1 第一条 `L 0,1`

Cache 初始为空：

```text
Set 0：没有有效 block
Set 1：没有有效 block
```

因此第一条访问没有命中，但也没有旧 block 可以驱逐：

```text
hits      不变
misses    +1
evictions 不变
```

同时目标 line 状态从：

```text
valid = 0
```

变成：

```text
valid = 1
保存地址 0 所属 block
```

### 4.2 第二条 `L 1,1`

由于 `b = 1`：

```text
B = 2^1 = 2 字节
```

因此：

```text
地址 0、1 属于同一个 block
```

第一条访问已经把这个 block 装入 Cache，所以第二条是 hit：

```text
hits      +1
misses    不变
evictions 不变
```

累计：

```text
hits      = 1
misses    = 1
evictions = 0
```

### 4.3 第三条 `L 2,1`

地址范围分组为：

```text
地址 0、1 → block 0
地址 2、3 → block 1
```

地址 2 与地址 0 不属于同一个 block。进一步计算 set 后：

```text
地址 0 → Set 0
地址 2 → Set 1
```

Set 1 仍然为空，所以第三条是：

```text
miss，但没有 eviction
```

### 4.4 第四条 `L 3,1`

地址 3 与地址 2 属于同一个 block，因此命中：

```text
hits +1
```

### 4.5 第五条 `S 4,1`

地址 4 属于新的 block：

```text
地址 4、5 → 一个 block
```

它映射回 Set 0，而 Set 0 中已经有地址 0、1 所属的 block；由于：

```text
E = 1
```

Set 0 没有第二条 line，所以发生：

```text
miss + eviction
```

此时统计变为：

```text
hits      = 2
misses    = 3
evictions = 1
```

### 4.6 第六条 `L 5,1`

地址 5 与地址 4 在同一个 block，且这个 block 已经被第五条访问装入 Set 0，因此命中：

```text
hits = 3
misses = 3
evictions = 1
```

### 4.7 手算过程中纠正的 set 范围错误

在分析 `S 6,1` 时，曾一度说地址 6 映射到 Set 2。后来根据：

```text
s = 1
S = 2^1 = 2
```

确认合法 set 只有：

```text
Set 0
Set 1
```

重新计算：

```text
set_index(6) = (6 >> 1) & 1
              = 3 & 1
              = 1
```

所以地址 6 映射到 Set 1，而不是 Set 2。Set 1 已经保存地址 2、3 所属 block，因此该访问是 miss + eviction。

这个错误形成了一个很重要的检查习惯：

```text
0 <= set_index < S
```

---

## 5. 第一步代码：解析命令行

首先没有直接写 Cache 逻辑，而是只让程序正确理解：

```text
-s
-E
-b
-t
-v
```

使用：

```c
getopt(argc, argv, "s:E:b:t:vh")
```

其中冒号表示该选项需要参数：

```text
s:、E:、b:、t:
```

没有冒号表示开关：

```text
v、h
```

例如命令：

```bash
./csim -s 2 -E 1 -b 3 -t traces/trans.trace
```

在进入程序时，命令行参数都是字符串：

```text
"2"
"1"
"3"
"traces/trans.trace"
```

处理 `-s 2` 时：

```text
optarg 指向字符串 "2"
        ↓
atoi(optarg)
        ↓
整数 2
```

这之后才能用于：

```text
S = 1 << s
```

调试输出曾使用：

```text
parsed: s=2 E=1 b=3 trace_file=traces/trans.trace verbose=0
```

参数顺序变化后仍然能得到相同解析结果，说明参数解析阶段工作正常。

过程中有一次命令拼写错误：

```bash
./cism
```

实际程序名是：

```bash
./csim
```

这只是 shell 命令拼写错误，不是代码故障。

---

## 6. 第二步代码：读取 trace 文件

使用：

```c
FILE *trace_fp = fopen(trace_file, "r");
```

打开失败时检查 `NULL`，并使用 `perror` 输出系统错误原因。

使用：

```text
" %c %llx,%d"
```

读取一条 trace：

```text
operation：L、S、M、I
address：十六进制地址
size：访问长度
```

格式字符串中：

- 开头空格：跳过 trace 中的空格和换行；
- `%c`：读取操作字符；
- `%llx`：按十六进制读取 64 位地址；
- 逗号：匹配输入中的字面量逗号；
- `%d`：读取访问长度。

将一次 `fscanf` 改成循环后，确认了程序可以继续读取整份 trace，而不是只读取第一条。

第一次发现“为什么没有打印指令”时，原因不是读取失败，而是运行命令中没有加：

```bash
-v
```

代码只有在：

```text
verbose == 1
```

时才打印。加上 `-v` 后，记录可以正常显示。

---

## 7. 第三步代码：过滤 `I`

读取成功后，先判断：

```text
operation == 'I'
```

如果成立，使用：

```text
continue
```

这样：

```text
I 不参与后续地址拆分
I 不参与 Cache 查找
I 不修改 LRU
I 不修改统计量
```

过滤位置必须在 Cache 模拟逻辑之前。

当时输出从包含 `I` 记录变成只保留：

```text
L
S
M
```

这验证了“读到了记录”和“参与数据 Cache 模拟”是两件不同的事情。

---

## 8. 第四步代码：设计三层 Cache 结构

最终采用的逻辑结构为：

```text
Cache
└── Set 数组
    └── Line 数组
```

### 8.1 Line

每条 line 包含：

```text
valid
 tag
 lru
```

`valid` 与 `tag` 必须一起判断命中：

```text
valid == 1 且 tag 相等 → hit
```

不能只判断 tag，因为初始化后的空 line 也可能恰好有 `tag = 0`。

### 8.2 Set

每个 Set 通过 `Line *lines` 指向本组的 `E` 条 line。

### 8.3 Cache

Cache 通过 `Set *sets` 指向 `S` 个 set，并保存：

```text
set_count
lines_per_set
```

### 8.4 为什么不保存真实 block 数据

Part A 不会真正读取或修改 block 中的字节；它只需要判断“这个 block 是否在 Cache 中”。因此只保存元数据即可：

```text
valid + tag + lru
```

这体现了系统模拟中的抽象：保留影响行为的状态，省略不影响评分目标的内容。

---

## 9. 第五步代码：动态分配和初始化

首先根据参数建立 Cache 几何：

```text
set_count = 1 << s
lines_per_set = E
```

然后做两层动态分配：

```text
第一层：为 S 个 Set 分配数组
第二层：为每个 Set 分配 E 条 Line
```

每次 `malloc` 后都检查是否返回 `NULL`。

每条 line 初始为：

```text
valid = 0
tag   = 0
lru   = 0
```

这里最重要的是 `valid = 0`：

```text
valid = 0 → 当前 line 没有可用于命中判断的 block
```

`malloc` 不会自动把内存初始化为零，因此必须显式初始化。否则未初始化的随机值可能导致：

```text
空 line 被误认为有效
随机 tag 导致错误 hit
随机 lru 导致错误 eviction
```

---

## 10. 第六步代码：地址拆分

地址结构为：

```text
| tag | set index | block offset |
```

计算公式：

```text
set_index = (address >> b) & set_mask
tag       = address >> (s + b)
```

set mask 提前计算：

```text
set_mask = (1ULL << s) - 1
```

使用 `1ULL` 是为了保证移位计算使用足够宽的无符号类型，而不是先按普通 `int` 计算。

手工验证的例子：

```text
address = 0x600aa0
s = 2
b = 3
```

推导：

```text
set_index = (0x600aa0 >> 3) & 0b11 = 0
 tag      = 0x600aa0 >> 5 = 0x30055
```

程序输出：

```text
set_index=0 tag=0x30055
```

与手算一致。

---

## 11. 第七步代码：查找 hit 和空 line

对每条数据访问，只扫描：

```text
cache.sets[set_index]
```

不需要扫描其他 set，因为 set index 已经确定了查找范围。

扫描时使用两个局部变量：

```text
hit_line = -1
empty_line = -1
```

它们的含义是：

```text
hit_line：匹配的有效 line 下标
empty_line：第一条无效 line 下标
```

扫描逻辑的优先级：

1. 如果 `valid == 1` 且 tag 匹配，记录 `hit_line` 并停止；
2. 如果 `valid == 0` 且还没有记录空 line，记录 `empty_line`；
3. 发现空 line 时不能立即停止，因为后面的 line 可能命中。

当程序刚开始运行时，所有 line 都是：

```text
valid = 0
```

因此第一次访问会得到：

```text
hit_line = -1
empty_line = 0
```

---

## 12. 第八步代码：由查找结果决定 hit、miss 和 eviction

本次调试中明确形成了下面这张核心表：

| `hit_line` | `empty_line` | 结果 |
|---:|---:|---|
| `>= 0` | 任意 | hit |
| `-1` | `>= 0` | miss，无 eviction |
| `-1` | `-1` | miss，有 eviction |

这张表的优先级是：

```text
hit 优先于 empty_line
empty_line 优先于 eviction
```

### 12.1 hit

如果：

```text
hit_line >= 0
```

则：

```text
hit_count++
命中 line 的 lru 更新为当前访问时间
```

### 12.2 miss，但有空 line

如果：

```text
hit_line == -1
empty_line >= 0
```

则：

```text
miss_count++
empty line.valid = 1
empty line.tag = 当前 tag
empty line.lru = 当前访问时间
eviction_count 不变
```

### 12.3 miss，且 set 已满

如果：

```text
hit_line == -1
empty_line == -1
```

则：

```text
miss_count++
eviction_count++
找 lru 最小的有效 line
替换 tag
更新 lru
```

---

## 13. 第九步代码：LRU

最初曾考虑直接使用 `line[0]` 作为被驱逐对象，但很快明确这是错误的：

```text
Line 下标只是存储位置
Line 下标不代表最近使用时间
```

例如：

| Line | lru |
|---:|---:|
| 0 | 10 |
| 1 | 3 |

即使 `line[0]` 下标更小，`line[1]` 的 `lru` 更小，说明它更久没有被使用，所以应驱逐 `line[1]`。

### 13.1 使用递增时间戳

程序设置：

```text
lru_count
```

每次真实数据 Cache 访问时递增：

```text
lru_count++
```

并将当前值写入被访问或新装入的 line：

```text
line.lru = lru_count
```

因此：

```text
lru 值越大 → 越晚使用
lru 值越小 → 越久未使用
```

满组时，在当前 set 内查找最小 `lru`。

### 13.2 `record_count` 和 `lru_count` 的区别

曾讨论是否直接把 `record_count` 改名成 `lru_count`，最后保留了两个概念：

| 计数器 | 含义 |
|---|---|
| `record_count` | trace 原始记录编号，可能包含 `I` |
| `lru_count` | 实际数据 Cache 访问顺序，不包含 `I` |

而且 `M` 需要两次 Cache 访问，所以：

```text
M 会使 lru_count 增加两次
```

这也是将 `lru_count++` 放入单次 Cache 访问函数，而不是只放在 trace 外层的原因。

---

## 14. 第十步代码：把单次访问抽成函数

随着逻辑增加，`main` 中同时包含：

```text
参数解析
Cache 分配
trace 读取
地址拆分
hit 查找
空 line 查找
LRU 替换
统计
verbose 输出
```

代码开始变乱，因此把“一次 Cache 访问”抽成：

```text
access_cache(address)
```

这个函数内部负责：

```text
递增 lru_count
计算 set_index 和 tag
查找 hit_line 和 empty_line
处理 hit
处理 miss + 空 line
处理 miss + LRU eviction
更新统计量
```

在设计上，曾考虑再创建一个 `Simulator` 结构体，用来传递 Cache、计数器和参数；但为了控制复杂度，最终没有创建。当前实现使用文件级全局状态：

```text
cache
s
b
set_mask
hit_count
miss_count
eviction_count
lru_count
```

这样 `access_cache` 不需要一长串参数。这个方案耦合较强，但对当前只有一个 Cache、一个 `main` 的实验来说足够清晰，也成功通过了测试。

重要的是：局部变量仍然保留在 `access_cache` 中：

```text
set_index
tag
hit_line
empty_line
eviction_line
min_lru
```

这些变量每次访问都重新计算，不需要提升为全局状态。

---

## 15. 第十一步代码：处理 `M`

最初只处理一遍 `M` 时，和参考答案相比少了一次 hit。

原因是：

```text
M 被错误地当成一次 Cache 访问
```

修正后主循环的逻辑是：

```text
L：access_cache 一次
S：access_cache 一次
M：access_cache 两次
```

于是：

```text
M f,1
```

可以产生：

```text
hit hit
```

或者：

```text
miss hit
```

这次修正同时保证：

```text
统计量增加正确
LRU 时间推进正确
第二次访问能看到第一次访问刚刚更新的 Cache 状态
```

---

## 16. 第十二步代码：verbose 输出

核心统计正确后，重新整理 verbose 输出。

最终采用的简单方案没有引入枚举返回值，而是：

```text
main 先输出：操作 地址,长度
access_cache 根据结果直接输出：hit / miss / miss eviction
main 最后换行
```

因此参考格式可以得到：

```text
L 0,1 miss
L 1,1 hit
S 4,1 miss eviction
M f,1 hit hit
```

这次选择不使用枚举常量，是为了保持当前实现易读；代价是 `access_cache` 同时负责模拟和 verbose 输出，模拟和显示之间耦合较强。对本次实验范围而言，这是可接受的工程取舍。

`I` 在进入 verbose 打印前就被 `continue`，所以 verbose 输出中不会出现 `I`。

---

## 17. Part B 基线：先让 `transpose_submit` 正确

Part B 开始时，`transpose_submit()` 为空，因此运行：

```bash
./test-trans -M 32 -N 32
```

得到：

```text
Validation error at function 0
Summary for official submission (func 0): correctness=0 misses=2147483647
```

这里的 `2147483647` 是 `INT_MAX` 哨兵值，不是真实 miss 数，表示 correctness 失败后没有进行性能统计。

随后先把 `transpose_submit()` 写成与基准函数等价的正确转置：

```text
A[i][j] → B[j][i]
```

测试结果变为：

```text
correctness=1
misses=1184
```

这一步确认了：

```text
数学转置正确
但访问顺序还没有优化
```

## 18. Part B Cache 参数和地址跨度推导

Part B 固定使用：

```text
s = 5
E = 1
b = 5
```

因此：

```text
32 个 set
每个 set 只有 1 条 line
每个 block = 32 字节
总 Cache 容量 = 32 × 32 = 1024 字节
```

每个 `int` 占 4 字节，所以一条 Cache block 能容纳：

```text
32 / 4 = 8 个 int
```

二维数组按 row-major order 存储。对于：

```c
int A[N][M];
```

固定行、列加一时：

```text
A[i][j] 和 A[i][j+1] 相差 4 字节
```

对于：

```c
int B[M][N];
```

访问：

```text
B[j][i] 和 B[j+1][i]
```

第一维变化了一行，因此相差：

```text
N × 4 字节
```

当矩阵为 32×32 时，B 的跨行距离为：

```text
32 × 4 = 128 字节
```

当矩阵为 64×64 时，B 的跨行距离为：

```text
64 × 4 = 256 字节
```

连续访问一行的 8 个 `int` 时，理论上可以得到：

```text
第一次访问 miss
后面最多 7 次 hit
```

前提是这 8 个元素没有跨越两个 block。

## 19. 第一版 blocking：32×32 使用 8×8 tile

基准代码的访问模式是：

```c
A[i][j] → B[j][i]
```

随着 `j` 增加：

```text
A 按行连续读取，局部性好
B 按列跨行写入，局部性差
```

因此把矩阵切成 8×8 tile：

```text
for 每个 block_row，每次加 8
    for 每个 block_col，每次加 8
        for 当前块内的 i
            for 当前块内的 j
                B[j][i] = A[i][j]
```

最初内层循环曾误写成：

```c
for (i = block_row; i < block_row; i++)
for (j = block_col; j < block_col; j++)
```

由于初始化值就不满足 `<` 条件，内层循环一次也不会执行。修正为当前块起点加块大小后，32×32 结果为：

```text
correctness=1
misses=344
```

## 20. 32×32 对角块优化

在直接映射 Cache 中：

```text
E = 1
```

同一个 set 只能保存一条 line。对角 tile 中，A 的读取和 B 的写入更容易互相竞争同一个 set，因此对角块单独处理。

曾出现一个控制流错误：对角块处理完使用了：

```c
break;
```

但 `break` 会结束最近的 `block_col` 循环，导致当前 `block_row` 后面的其他列块全部被跳过，B 有大量元素没有写入。后来改为：

```c
continue;
```

其语义是：

```text
当前对角块已经处理完
跳过后面的普通块代码
继续下一个 block_col
```

普通 8×8 循环不能删除，因为它仍然负责非对角块。

优化后 32×32 的实际结果为：

```text
初版 8×8：344 misses
加入对角块特殊处理：288 misses
```

官方阈值为：

```text
32×32：misses < 300
```

所以：

```text
32×32 达到满分条件：8/8
```

## 21. 64×64 的异常：同样的 8×8 却大幅恶化

把 32×32 的 8×8 策略直接用于 64×64 后：

```text
correctness=1
misses=4612
```

而简单按行基准函数是：

```text
misses=4724
```

说明简单 8×8 只比基准略好，远未达到：

```text
64×64：misses < 1300
```

### 21.1 64×64 的行跨度

64×64 中一行占：

```text
64 × 4 = 256 字节
```

也就是：

```text
256 / 32 = 8 个 Cache block
```

四行相差：

```text
4 × 256 = 1024 字节
```

而 Cache 总容量正好是：

```text
32 × 32 = 1024 字节
```

因此：

```text
A[0][0] 和 A[4][0]
```

相差 32 个 block，set index 相同；更准确地说，它们相差整个 Cache 容量，所以会映射到同一个 set。由于 `E=1`，后访问的 block 会驱逐先访问的 block。

这里的“跨越整个 Cache 容量”不是说一定因为容量不足，而是说明地址的 set index 经过模 32 后重新回到同一个值；真正的冲突原因是：

```text
同一个 set + 直接映射
```

## 22. 简单 4×4 实验：明显改善但仍不够

随后测试了把整个 64×64 矩阵直接切成简单 4×4 tile。第一次代码曾有两个错误：

1. 外层步长是 4，但内层范围误写成 `+8`，导致相邻 tile 重叠；
2. 最后 tile 存在边界越界风险，必须保证 `i < N`、`j < M`。

修正为真正的 4×4 范围后，结果为：

```text
correctness=1
misses=1892
```

这说明 4×4 的局部工作集确实降低了部分冲突：

```text
8×8 简单分块：4612 misses
4×4 简单分块：1892 misses
```

但仍然超过官方阈值：

```text
1892 > 1300
```

因此“把 tile 缩小”不是完整答案。

## 23. 关键突破：简单 4×4 的 B/B 自我驱逐

本次对 64×64 的深入分析中，发现不能只盯着 A 与 B 的冲突。简单 4×4 还有一个对所有区域都可能产生影响的缺陷：

> **B 自己的不同部分也会互相驱逐。**

以 64×64 的 B 为例：

```text
B 的一行 = 64 × 4 = 256 字节
```

B 的第 `r` 行和第 `r+4` 行相差：

```text
4 × 256 = 1024 字节
```

这正好等于整个 Cache 容量，因此它们映射到相同 set。

简单 4×4 在处理同一组 A 行时，访问模式类似：

```text
先处理一个 4×4 tile：写 B[0..3][0..3]
再处理下一个 4×4 tile：写 B[4..7][0..3]
```

由于：

```text
B[0..3] 与 B[4..7]
```

的对应 block 可能映射到相同 set，后一个区域会把前一个区域的 Cache line 驱逐。

更糟糕的是，一条 Cache block 能容纳：

```text
8 个 int
```

但简单 4×4 每次只写一行中的 4 个 `int`，即只填了一条 Cache line 的一半：

```text
4 × 4 = 16 字节
```

于是可能出现：

```text
1. 写 B[r][0..3]，只填半条 line
2. 写 B[r+4][0..3]，映射到相同 set，驱逐前一条
3. 后续再写 B[r][4..7] 时，前半条 line 已经不在 Cache 中
4. 相关 block 需要重新处理，miss 增加
```

因此简单 4×4 的问题不只是“对角块 A/B 冲突”，还包括：

```text
B/B 行间 set 冲突
+ 每次只利用半条 Cache line
+ 直接写最终位置，没有暂存和交换
```

## 24. 为什么 8×8 宏观块内还要拆成 4×4

这里的“8×8 内部拆成 4×4”与“整个矩阵简单使用 4×4”完全不同。

### 简单 4×4

```text
整个矩阵按 4×4 切块
每个 4×4 直接转置
直接写入 B 的最终位置
没有暂存和交换
```

### 8×8 宏观块 + 4×4 内部重排

```text
把一个 8×8 tile 作为整体工作单元
上半部分每行读取 8 个 int
A11 直接写 B11
A12 先写入暂存区
下半部分读取 A21
把暂存的 A12 搬到最终 B21
把 A21 写入腾出的区域
A22 写入 B22
```

四个逻辑子块的最终映射是：

```text
A11 → B11
A12 → B21
A21 → B12
A22 → B22
```

第一阶段的暂存关系是：

```text
A11 → B11，最终位置
A12 → B12 的暂存位置
```

第二阶段再完成：

```text
暂存的 A12 → B21 最终位置
A21 → 原暂存位置 B12
A22 → B22
```

这个策略的关键不是保证冲突完全消失，而是：

```text
先把暂存数据读取到局部变量
→ 即使之后 A21 的读取驱逐了 B 暂存 block，数据也不会丢失
→ 用 A21 覆盖暂存位置
→ 把局部变量中的 A12 写入最终位置
```

这里必须先读暂存数据，再读取 A21。否则 A21 可能先驱逐 B 暂存 block，之后再读暂存位置会产生额外 miss。

## 25. 64×64 两阶段搬运的坐标推导

设当前 8×8 tile 起点为：

```text
row = block_row
col = block_col
```

### 25.1 阶段一：上 4 行

对：

```text
A[i][col+0 ... col+7]
```

读取 8 个值，分成：

```text
前 4 个：A11
后 4 个：A12
```

A11 直接写入：

```text
A[i][col+0] → B[col+0][i]
A[i][col+1] → B[col+1][i]
A[i][col+2] → B[col+2][i]
A[i][col+3] → B[col+3][i]
```

A12 暂时写入：

```text
A[i][col+4] → B[col+0][i+4]
A[i][col+5] → B[col+1][i+4]
A[i][col+6] → B[col+2][i+4]
A[i][col+7] → B[col+3][i+4]
```

例如：

```text
A[row][col+4]
```

根据转置公式，其最终位置是：

```text
B[col+4][row]
```

但阶段一暂时放在：

```text
B[col][row+4]
```

这是 A21 对应元素未来要占用的位置。

### 25.2 阶段二：交换 A12 和 A21

以 `j=0` 为例，阶段一留下：

```text
B[col][row+4] = A[row][col+4]
B[col][row+5] = A[row+1][col+4]
B[col][row+6] = A[row+2][col+4]
B[col][row+7] = A[row+3][col+4]
```

而 A21 的对应数据为：

```text
A[row+4][col]
A[row+5][col]
A[row+6][col]
A[row+7][col]
```

它们最终要写入：

```text
B[col][row+4]
B[col][row+5]
B[col][row+6]
B[col][row+7]
```

所以必须按以下顺序：

```text
1. 先读取 B[col][row+4 ... row+7] 中旧的 A12
2. 把旧值保存到局部变量
3. 再读取 A[row+4 ... row+7][col] 中的 A21
4. 用 A21 覆盖 B[col][row+4 ... row+7]
5. 把局部变量中的旧 A12 写入 B[col+4][row ... row+3]
```

这不是因为冲突会让内存数据消失。需要区分：

```text
Cache line 被驱逐：只是 Cache 中不再保留该 block
B 数组中的值被覆盖：才是内存数据被改变
```

A21 的读取即使驱逐了 B 的暂存 block，也不影响已经保存到局部变量中的 A12。

对一般 `j=0..3`，坐标模式是：

```text
旧 A12 暂存读取：B[col+j][row+4 ... row+7]
A21 读取：      A[row+4 ... row+7][col+j]
A21 写入：      B[col+j][row+4 ... row+7]
旧 A12 最终写入：B[col+4+j][row ... row+3]
A22 读取：      A[row+4 ... row+7][col+4+j]
A22 写入：      B[col+4+j][row+4 ... row+7]
```

## 26. 61×67 基线：正确性失败的原因

在完成 32×32 和 64×64 后，运行：

```bash
./test-trans -M 61 -N 67
```

结果为：

```text
Validation error at function 0
Summary for official submission (func 0): correctness=0 misses=2147483647
```

当前 `transpose_submit()` 只处理了两个显式分支：

```text
M == 32 && N == 32
M == 64 && N == 64
```

对于 `M=61、N=67`，两个条件都不成立，函数没有进入任何转置循环，因此 `B` 没有被完整填充。这里的 `2147483647` 仍然是 correctness 失败时使用的 `INT_MAX` 哨兵值，不是真实 miss 数。

这一阶段暴露出两个新的问题：

1. 必须为非 32×32、非 64×64 的矩阵提供通用正确性路径；
2. `61` 和 `67` 都不能被 `8` 整除，最后一个 tile 不能继续使用固定的 `block_row + 8`、`block_col + 8` 作为无条件访问范围，否则可能越界。

当前应先建立一个边界安全的通用转置逻辑，再测量 61×67 的 miss；不要直接把 64×64 的专用两阶段交换代码套到不完整 tile 上。

通用边界推导：

```text
M = 61：合法列下标 0..60
N = 67：合法行下标 0..66
```

8×8 tile 起点可能包括：

```text
block_row = 0, 8, ..., 64
block_col = 0, 8, ..., 56
```

最后的 tile 只能访问仍满足：

```text
i < N
j < M
```

而不能假设：

```text
i < block_row + 8
j < block_col + 8
```

## 27. 61×67 块大小实验：最终选择 17×17

边界安全的简单 8×8 通用转置已经通过 correctness，但结果为：

```text
misses = 2119
```

官方 61×67 的满分阈值是：

```text
misses < 2000
```

随后对通用分支进行了块大小实验，保留同样的转置关系和边界判断：

```text
B[j][i] = A[i][j]
```

实测结果为：

```text
8×8：  misses=2119
16×16：misses=1993
17×17：misses=1951
```

因此最终选择 17×17 通用分块。它得到：

```text
correctness=1
misses=1951
```

满足：

```text
1951 < 2000
```

所以 61×67 达到官方满分阈值（10/10）。

### 27.1 为什么不是简单 8×8

8×8 具有明显优点：一行 8 个 `int` 正好对应一条 32 字节 Cache block。但 61×67 不是 8 的整数倍：

```text
61 = 7×8 + 5
67 = 8×8 + 3
```

所以边界会产生不完整 tile。最后一块只能使用部分 Cache line，且多个小 tile 会把同一行的访问切散，造成更多重复访问和边界开销。8×8 的实测结果是：

```text
2119 misses
```

### 27.2 为什么 16×16 和 17×17 都值得实验

这次结果表明，块大小选择不是简单的“越接近 Cache block 就越好”。实测候选为：

| 通用块大小 | correctness | misses | 结论 |
|---:|---:|---:|---|
| 8×8 | 1 | 2119 | 超过 2000，未达标 |
| 16×16 | 1 | 1993 | 刚好低于 2000，达标 |
| 17×17 | 1 | 1951 | 更低，最终采用 |

16×16 并不是因为一个 tile 能完整放入 Cache。一个 16×16 的 `int` tile 包含：

```text
16×16×4 = 1024 字节
```

这已经等于整个 1KB Cache，A 和 B 不可能同时完整驻留。

16×16 的优势来自访问轨迹的折中：

- 相比 8×8，外层 tile 数量更少；
- 同一行的连续访问被组织得更集中；
- 非边界区域减少了被多个小 tile 切割的机会；
- 61×67 的边缘只产生少量不完整 tile，边界安全条件仍然保证不越界；
- 该尺寸不像 64×64 那样具有强烈的规则化 4 行 set 重复，因此不需要 64×64 的专用 A12/A21 交换。

17×17 的选择也不是由某个单独公式必然推出的，而是通过候选实验发现的。这里的最优点受：

```text
Cache block 利用率
+ tile 切换次数
+ A/B 冲突
+ 边界碎片
```

之间的实测折中。最终通过 `test-trans -M 61 -N 67` 的结果验证，而不是只凭直觉。

### 27.3 为什么暂时不用 32×32

32×32 tile 的完整数据量为：

```text
32×32×4 = 4096 字节
```

是 Cache 容量的四倍。虽然更大的 tile 会进一步减少外层循环次数，但它的工作集远超 Cache，A 和 B 的 block 会频繁互相驱逐，局部性收益会被冲突抵消。

此外，61×67 的边界使用 32×32 时会产生更大的不完整区域；最后 tile 内访问的有效数据比例更低，可能增加无效竞争。块过大时，减少循环次数的收益通常不如增加的 Cache 竞争代价。

### 27.4 为什么不需要在 16×16 内部再次拆块

当前 61×67 的 16×16 方案是一个**简单的边界安全 blocking**：

```text
16×16 tile
→ 在 tile 内直接执行 A[i][j] → B[j][i]
```

它不需要像 64×64 那样再做 8×8 外块、4×4 内部重排，原因是：

- 64×64 的特殊问题来自 64 行跨度、32 个 set 和 `E=1` 的规则性冲突；
- 61×67 的行跨度与 Cache set 的关系不形成同样整齐的 4 行重复周期；
- 17×17 简单分块已经把 miss 降到 1951，满足 `<2000`，且优于 16×16 的 1993；
- 再增加内部交换会提高代码复杂度，并可能触碰局部变量和边界处理限制，没有必要。

因此当前选择遵循工程原则：

```text
先用简单方案测量
达到评分阈值后停止继续复杂化
```

### 27.5 8×8、16×16、17×17、32×32 的对比

| 通用块大小 | 结果 | 解释 |
|---:|---:|---|
| 8×8 | 2119 misses | 正确，但边界碎片和 tile 切换较多，超过 2000 阈值 |
| 16×16 | 1993 misses | 正确且低于 2000，刚好达到满分条件 |
| 17×17 | 1951 misses | 正确且优于 16×16，最终采用 |
| 32×32 | 未作为最终方案采用 | 工作集为 4KB，远超 1KB Cache，冲突和边界代价更大 |

## 28. 32×32 / 64×64 / 61×67 最终策略对照

本次最终提交函数采用了按矩阵规模分支的策略：

```text
M=32,N=32：8×8 blocking + 对角块专门处理
M=64,N=64：8×8 宏观块 + 4×4 内部重排
其他规模：17×17 blocking + 边界保护
```

这不是代码重复，而是针对三种不同地址映射结构的专项优化：

- 32×32 的块行号和块列号都落在 `0..3`，A/B 非对角 tile 的 set 余数错开，对角 tile 才会完全重合；
- 64×64 的每行跨度为 256 字节，4 行跨度正好等于 1KB Cache 容量，出现强烈的 4 行 set 重复，需要暂存和交换；
- 61×67 的行跨度分别为 244 和 268 字节，边界不整齐但没有相同的 64×64 规则冲突周期，17×17 简单 blocking 已经达到更好的实测结果。

最终成绩不是由“一个固定块大小适用于所有矩阵”得到的，而是通过：

```text
先建立正确性版本
→ 对每种规模测量 miss
→ 推导地址映射规律
→ 只对存在结构性冲突的规模增加复杂度
→ 达标后停止继续优化
```

## 29. 当前 Part B 的真实进度和未完成事项

已完成：

```text
32×32 正确性
32×32 8×8 blocking
32×32 对角块处理
64×64 正确性
64×64 简单 8×8 实验
64×64 简单 4×4 实验
64×64 的 8×8 宏观块 + 4×4 内部重排
61×67 的边界安全处理
61×67 的 8×8 与 16×16 性能对比
```

关键实验结果：

```text
32×32：8×8 + 对角块处理，288 misses
64×64：简单 8×8，4612 misses
64×64：简单 4×4，1892 misses
64×64：8×8 外块 + 4×4 内部重排，1220 misses
61×67：边界安全 8×8，2119 misses
61×67：边界安全 16×16，1993 misses
61×67：边界安全 17×17，1951 misses
```

三个官方矩阵规模均满足性能阈值：

```text
32×32：288 < 300
64×64：1220 < 1300
61×67：1951 < 2000
```

因此 Part B 的三个性能项目均达到满分条件。`driver.py` 是旧版 Python 2 风格脚本，本次主要使用 `test-csim` 和三个 `test-trans` 命令完成验证。

当前阶段已经完成并确认无误的 64×64 阶段一是：

```text
对每个 8×8 tile 的上 4 行：
    读取一行 8 个 A 元素
    前 4 个写入 B11 最终位置
    后 4 个写入 B12 暂存位置
```

随后完成了阶段二和 A22 写入：

```text
对 j = 0..3 的每一列：
    先从 B12 读取暂存的 A12
    再按列读取 A21
    A21 写回 B12 暂存位置
    旧 A12 写入 B21 最终位置
    读取 A22 并写入 B22
```

64×64 的最终测试结果为：

```text
correctness=1
misses=1220
evictions=1188
```

官方阈值是 `misses < 1300`，因此 64×64 已达到该项满分条件（8/8）。

阶段一的核心代码逻辑为：

```text
v0...v7 = A[i][block_col ... block_col+7]

B[block_col+0][i] = v0
B[block_col+1][i] = v1
B[block_col+2][i] = v2
B[block_col+3][i] = v3

B[block_col+0][i+4] = v4
B[block_col+1][i+4] = v5
B[block_col+2][i+4] = v6
B[block_col+3][i+4] = v7
```

这里的坐标已经逐项检查正确；下一步需要继续实现阶段二的 A12/A21 交换和 A22 写入。

---

## 29. 32×32：严格推导为什么非对角块 A/B 不冲突、对角块会冲突

下面的推导针对 Part B 的固定 Cache：

```text
32 个 set
E = 1
每条 block = 32 字节
每个 int = 4 字节
```

因此 Cache set 可以由相对于矩阵首地址的字节偏移近似表示为：

```text
Set = (字节偏移量 / 32) mod 32
```

这里的结论建立在实验框架中 A、B 的起始地址低位对齐关系不改变 set 余数比较的前提下；实际完整地址的高位 tag 不影响 set index 的模运算。

### 29.1 32×32 划分

把 32×32 矩阵划分为 4×4 个 8×8 tile。令：

```text
I：块行号，I ∈ {0,1,2,3}
J：块列号，J ∈ {0,1,2,3}
k：tile 内的行号，k ∈ {0,...,7}
```

### 29.2 A 的第 `(I,J)` tile

A 的第 `(I,J)` tile 中第 `k` 行起点的元素下标为：

```text
行 = 8I + k
列 = 8J
```

相对于 A 首地址的字节偏移：

```text
Offset_A
= ((8I + k) × 32 + 8J) × 4
= 1024I + 128k + 32J
```

除以 block 大小 32 并模 32：

```text
Set_A
= ((1024I + 128k + 32J) / 32) mod 32
= (32I + 4k + J) mod 32
= (4k + J) mod 32
```

因为 `32I mod 32 = 0`，A tile 使用的 set 满足：

```text
Set_A ≡ J (mod 4)
```

也就是说，A 的 `(I,J)` tile 使用的 set 集合是：

```text
{J, J+4, J+8, ..., J+28}
```

### 29.3 B 的转置目标 tile

A 的 `(I,J)` tile 转置后写入 B 的 `(J,I)` tile。B 的第 `(J,I)` tile 中第 `k'` 行起点元素下标为：

```text
行 = 8J + k'
列 = 8I
```

相对于 B 首地址的字节偏移：

```text
Offset_B
= ((8J + k') × 32 + 8I) × 4
= 1024J + 128k' + 32I
```

因此：

```text
Set_B
= ((1024J + 128k' + 32I) / 32) mod 32
= (32J + 4k' + I) mod 32
= (4k' + I) mod 32
```

所以 B 的目标 tile 使用的 set 满足：

```text
Set_B ≡ I (mod 4)
```

### 29.4 非对角 tile：集合不相交

当：

```text
I != J
```

A 的 set 余数为：

```text
Set_A ≡ J (mod 4)
```

B 的 set 余数为：

```text
Set_B ≡ I (mod 4)
```

因为在 32×32 的块编号中 `I、J ∈ {0,1,2,3}`，所以 `I != J` 时它们的模 4 余数不同。因此：

```text
Set_A ∩ Set_B = ∅
```

这表示针对 A/B 的直接映射冲突没有交集。

例如 `I=0、J=1`：

```text
A：(0,1,5,9,13,17,21,25,29)
B：(0,4,8,12,16,20,24,28)
```

更准确地按集合写为：

```text
A 使用：{1,5,9,13,17,21,25,29}
B 使用：{0,4,8,12,16,20,24,28}
```

两者没有交集，因此非对角 tile 中，A 的读取 block 和 B 的目标 block 不会因为这组 A/B 映射关系直接争用同一 set。

### 29.5 对角 tile：集合完全重合

当：

```text
I = J
```

有：

```text
Set_A ≡ I (mod 4)
Set_B ≡ I (mod 4)
```

因此二者使用同一组 set。例如 `I=J=0`：

```text
A 使用：{0,4,8,12,16,20,24,28}
B 使用：{0,4,8,12,16,20,24,28}
```

在直接映射 Cache 中，A 与 B 交替访问时，后访问的 block 可能驱逐先访问的 block，形成对角块冲突。因此 32×32 使用对角块特殊处理，最终从 344 misses 降到 288 misses。

### 29.6 这条结论的适用范围

这条“非对角 A/B set 集合完全不相交”的严格结论是针对：

```text
32×32 矩阵
8×8 tile
s=5、E=1、b=5
块行/块列编号 I、J ∈ {0,1,2,3}
```

不能把它直接推广到 64×64。64×64 的块编号和行跨度不同，存在相隔 4 行重新映射到同一 set 的规律；这正是 64×64 需要 8×8 外块加 4×4 内部重排的原因之一。

---

# 模块 B：核心系统知识点深度提炼

## 1. Cache 地址分解

地址分为：

```text
| tag | set index | block offset |
```

其中：

- block offset 决定 block 内的字节位置；
- set index 决定进入哪个 set；
- tag 在选定的 set 内识别具体 block。

因此命中不是单纯比较完整地址，而是：

```text
先由 set index 缩小范围
再在该 set 的 E 条 line 中比较 valid 和 tag
```

## 2. 左移、右移和掩码

```text
1 << s
```

表示：

```text
2^s
```

用于计算 set 数量。

```text
(address >> b)
```

丢弃低 `b` 位 block offset。

```text
(1ULL << s) - 1
```

生成低 `s` 位为 1 的掩码。

```text
(address >> b) & set_mask
```

保留下一个 `s` 位，得到 set index。

```text
address >> (s + b)
```

同时移除 block offset 和 set index，得到 tag。

## 3. `valid` 是独立状态

不能用：

```text
tag == 0
```

表示空 line，因为有效 block 的 tag 也可能是 0。

正确判断必须包含：

```text
valid == 1 && tag 相等
```

## 4. 直接映射与组相联

当：

```text
E = 1
```

每个 set 只有一条 line，是直接映射 Cache。

当：

```text
E > 1
```

每个 set 有多条 line，是组相联 Cache，需要：

```text
在同一 set 内搜索 tag
```

并在满组 miss 时执行 LRU 替换。

## 5. LRU 软件模拟

真实硬件可能使用专门的替换元数据，而本实验使用简单时间戳：

```text
每次真实访问时全局计数器加一
被访问 line 保存当前时间
满组时找时间戳最小者
```

它把“最近最少使用”转成了一个可比较的整数问题。

## 6. `malloc` 与多级指针

当前数据结构中：

```text
Cache.sets → S 个 Set
Set.lines  → E 个 Line
```

因此需要两层动态分配。只分配 `sets` 并不意味着 `sets[i].lines` 已经有效；每个 `lines` 指针还需要单独指向一个动态数组。

`malloc` 只分配原始内存，不负责初始化字段，因此必须显式设置：

```text
valid = 0
tag = 0
lru = 0
```

## 7. trace/replay 的思想

程序不是重新生成原始访存，而是读取已经记录好的访问轨迹并重放。这种方法把复杂运行行为转换成可重复、可比较的输入序列，方便：

```text
逐条对比
定位第一次状态分歧
确认某次 miss 的原因
```

## 8. `M` 是复合状态转移

`M` 并不需要独立设计一套 Cache 规则，而是复用普通访问规则两次：

```text
M → access(load) → access(store)
```

这样可以避免复制两份查找和替换代码，并确保第二次访问看到第一次访问更新后的 Cache 状态。

---

# 模块 C：典型踩坑、错误现象和排查范式

## 1. 把空程序的全零输出误认为完成

### 错误现象

```text
hits:0 misses:0 evictions:0
```

### 根因

原始 `csim.c` 只调用了：

```text
printSummary(0, 0, 0)
```

没有解析参数、读取 trace 或模拟 Cache。

### 排查范式

```text
用 csim-ref 跑相同参数
→ 对比参考统计
→ 确认学生程序只是占位
```

## 2. APT 卡在等待报头

### 错误现象

```text
[已连接到 archive.ubuntu.com] [正在等待报头]
```

### 根因

软件源连接建立后，HTTP 响应迟迟没有返回。可能涉及镜像、网络、IPv6、代理或 WSL DNS。

### 排查范式

```text
Ctrl+C 安全中断
→ 尝试 ForceIPv4 和超时参数
→ 用 curl 检查镜像可达性
→ 必要时更换镜像源
```

之后 Valgrind 安装成功，Part A 不受影响，Part B 环境也准备完成。

## 3. 命令拼写错误

### 错误现象

```text
bash: ./cism: 没有那个文件或目录
```

### 根因

程序名输入错误，正确名称是：

```text
./csim
```

### 排查范式

```text
ls 或补全检查可执行文件名
→ 对照 Makefile target
→ 重新运行正确命令
```

## 4. `-v` 未输出记录

### 错误现象

运行普通命令时没有逐条 trace 输出：

```bash
./csim -s 2 -E 1 -b 3 -t traces/trans.trace
```

### 根因

代码设计为只有：

```text
verbose == 1
```

才打印，而命令中没有加 `-v`。

### 修正

```bash
./csim -v -s 2 -E 1 -b 3 -t traces/trans.trace
```

## 5. 手算时把地址映射到不存在的 Set

### 错误现象

在：

```text
s = 1
```

时曾把地址 6 映射到 Set 2。

### 根因

忽略了：

```text
S = 2^1 = 2
```

合法下标只有 0 和 1。

### 修正

重新使用：

```text
set_index = (address >> b) & ((1 << s) - 1)
```

并检查：

```text
0 <= set_index < S
```

## 6. `empty_line` 一直是 -1

### 错误现象

所有未命中都被判断成 eviction。

### 根因

只写了：

```text
empty_line = -1
```

却没有在扫描过程中遇到 `valid == 0` 时记录下标。

### 修正

扫描每条 line 时：

```text
valid == 0 且还没有记录空 line
→ empty_line = 当前下标
```

## 7. 担心前面发现空 line 会错误覆盖后面的 hit

### 问题

如果前面的 Line 为空、后面的 Line 命中，会不会因为 `empty_line` 已有值而错误增加 eviction？

### 结论

不会，只要判断顺序正确：

```text
先判断 hit_line
再判断 empty_line
最后才判断 eviction
```

这就是核心表格的意义：

| `hit_line` | `empty_line` | 结果 |
|---:|---:|---|
| `>= 0` | 任意 | hit |
| `-1` | `>= 0` | miss，无 eviction |
| `-1` | `-1` | miss，有 eviction |

## 8. 直接使用 `line[0]` 作为 victim

### 错误思路

```text
下标最小，所以淘汰 line[0]
```

### 根因

下标只是存储位置，不是使用顺序。

### 修正

扫描同一 Set 的所有 line：

```text
找 lru 最小的有效 line
```

## 9. `M` 只处理一次导致少一次 hit

### 错误现象

自己的统计与参考程序相比少一次 hit。

### 根因

把：

```text
M
```

当成一次访问，而官方定义是：

```text
Load + Store
```

### 修正

对同一个地址调用两次单次访问逻辑：

```text
第一次：Load
第二次：Store
```

## 10. `record_count` 与 LRU 计数混用

### 问题

`record_count` 在过滤 `I` 之前递增，所以它包含 instruction record；而 LRU 只应该记录真实数据访问。

### 修正

保留两个概念：

```text
record_count：trace 行号
lru_count：数据 Cache 访问序号
```

并让 `lru_count` 在单次 Cache 访问函数内部递增，这样 `M` 会推进两次。

## 11. 代码越改越乱，局部变量提取问题

### 现象

`main` 同时包含参数解析、内存分配、trace 读取、查找、替换和统计，开始出现大量局部变量与注释代码。

### 处理过程

先考虑创建 `Simulator` 结构体，把所有状态集中传递；后来为了降低复杂度，选择了文件级全局状态，并把单次访问逻辑抽取为：

```text
access_cache(address)
```

### 工程取舍

当前方案不是最通用的架构，但优点是：

```text
access_cache 参数少
main 只负责读取和分派
局部变量仍然留在访问函数内部
```

对于本次单一模拟器实验，最终通过测试，说明该取舍满足任务需求。

## 12. 当前代码还存在的工程完善项

Part A 核心测试虽然通过，但仍可继续完善：

- 正常退出前显式释放每个 Set 的 `lines`，再释放 `cache.sets`；
- 处理部分分配失败时的已分配内存回收；
- 为 `-h` 增加 usage 输出；
- `default` 分支对未知参数给出错误；
- 检查 `s`、`E`、`b` 的合法范围；
- 区分 trace 正常 EOF 与格式错误；
- 将全局变量改成 `static` 或上下文结构体以限制作用域。

这些是工程质量改进，不影响本次 `test-csim` 的正确性结果。

---

# 模块 D：AI Infra 底层映射与工程直觉

## 1. Cache set/tag 查找与 KV Cache block 管理

Part A 的查询流程：

```text
地址
→ 定位 set
→ 在 set 中查找 tag
→ 命中则复用
→ 未命中则分配或驱逐
```

与推理系统中的 KV Cache block 管理具有相似的状态机：

```text
请求映射到缓存块
→ 查找已有块
→ 命中则复用历史状态
→ 未命中则申请新块
→ 容量不足时选择牺牲块
→ 更新元数据
```

真实推理引擎可能按 sequence、page 或 block table 索引，而不一定使用硬件 Cache 的 tag/set 格式，但“索引、命中、替换、元数据更新”的思路一致。

## 2. LRU 与推理服务缓存淘汰

本实验用时间戳实现 LRU：

```text
越晚访问，时间戳越大
最小时间戳作为 victim
```

在模型服务中，缓存淘汰可能考虑：

- 最近访问时间；
- 访问频率；
- token 数量；
- GPU 显存压力；
- 请求优先级；
- 重新计算成本。

虽然真实策略可能从 LRU 变成 LFU、分层缓存或成本感知策略，但基本流程仍然是：

```text
缓存元数据维护
→ 命中更新元数据
→ 未命中选择 victim
→ 替换并更新状态
```

## 3. PyTorch 内存分配器

本次 Cache 的动态分配过程是：

```text
根据几何参数分配外层 set
→ 再为每个 set 分配 line
→ 初始化元数据
```

这对应内存分配器中的分层管理思想：

```text
大区域
→ 子区域
→ 元数据块
→ 可用/占用状态
```

本实验也暴露了两个生产环境中同样重要的问题：

- 分配失败必须检查；
- 所有权明确后必须释放；
- 未初始化元数据会导致非确定性行为。

## 4. CUDA Kernel 访存与矩阵转置

Part A 通过地址流计算 Cache miss；Part B 将继续研究矩阵转置访问顺序。对应到 CUDA：

```text
线程访问是否连续
→ memory transaction 是否合并
→ Cache line 利用率如何
→ 是否发生 bank conflict 或 cache conflict
```

矩阵按行和按列访问的差异，正是“数学结果相同，但访存成本不同”的典型例子。

### 4.1 Cache set 冲突与 Shared Memory bank conflict

本次 32×32 推导中，关键结构是：

```text
地址
→ 除以 block 大小得到 block 编号
→ 对 set 数取模
→ 决定落在哪个 set
```

CUDA Shared Memory 也存在相似的离散映射问题：片上 SRAM 被划分为多个 bank。一个 warp 中的线程如果同时访问同一个 bank 的不同地址，就会发生 bank conflict，访问请求需要被拆分处理。

两者的共同工程直觉是：

```text
物理存储被划分为有限的并行槽位
→ 地址通过低位或取模映射到槽位
→ 不同逻辑数据可能落到同一槽位
→ 访问模式需要主动错开映射
```

### 4.2 Padding 与取模错开

典型的 CUDA 转置会把共享内存数组从 32 列改成 33 列：

```cuda
__shared__ float tile[32][33];
```

增加一列 padding 后，下一行的起始地址不再以原来的 stride 重复映射到同一个 bank。抽象地说，原本的 stride 可能满足：

```text
stride mod bank_count = 0
```

加入 padding 后变为：

```text
(stride + 1) mod bank_count != 0
```

相邻行或相邻线程的 bank 编号因此被错开，降低或消除 bank conflict。

这和 Cache Lab 中通过改变 tile 内部访问顺序、让 A/B 的 set 使用错开，是同一种底层优化思想：

```text
不是改变数学结果
而是改变数据到有限硬件槽位的映射关系
```

### 4.3 与本次 32×32 推导的对应关系

本次推导得到：

```text
32×32 非对角 tile：A 的 set 余数是 J，B 的 set 余数是 I
I != J 时二者错开

32×32 对角 tile：I = J
A/B 使用同一组 set
```

CUDA padding 的目标也是让原本相同或周期性重合的 bank 映射变成错开映射。两者细节不同：

```text
Cache set：由 block 地址映射，可能伴随 tag 和替换
Shared Memory bank：由共享内存地址映射，重点是 warp 内并行访问冲突
```

但都要求从：

```text
数据布局 + stride + 模映射规律
```

推导性能瓶颈，而不是只看算法的算术复杂度。

## 5. Blocking/Tiling 与 FlashAttention

Part B 的 blocking 思路可以映射到 FlashAttention 的 tile 计算：

```text
不要反复访问完整矩阵
→ 把当前工作块留在较快层级
→ 在块内完成更多计算
→ 减少慢速内存往返
```

Cache Lab 中的 block 是硬件 Cache line/Cache 容量约束；FlashAttention 中的 tile 是片上 SRAM/shared memory/register 约束。两者的共同工程直觉是：

```text
算术操作本身不一定是瓶颈
数据移动和局部性经常才是瓶颈
```

## 6. Trace/replay 与系统性能分析

本实验使用 trace 重放，而不是只运行一次程序后猜性能原因。这对应现代系统中的：

- 请求 trace 重放；
- GPU kernel 访存 trace；
- KV Cache 命中率统计；
- 分布式存储访问 replay；
- 推理服务 cache hit ratio 评估。

重要的工程方法是：

```text
记录行为
→ 重放行为
→ 找到第一次状态分歧
→ 用最小样例验证假设
→ 再扩大到完整 workload
```

---

# 最终结果与完成状态

## Part A 验证结果

运行：

```bash
./test-csim
```

得到：

```text
                        Your simulator     Reference simulator
Points (s,E,b)    Hits  Misses  Evicts    Hits  Misses  Evicts
     3 (1,1,1)       9       8       6       9       8       6  traces/yi2.trace
     3 (4,2,4)       4       5       2       4       5       2  traces/yi.trace
     3 (2,1,4)       2       3       1       2       3       1  traces/dave.trace
     3 (2,1,3)     167      71      67     167      71      67  traces/trans.trace
     3 (2,2,3)     201      37      29     201      37      29  traces/trans.trace
     3 (2,4,3)     212      26      10     212      26      10  traces/trans.trace
     3 (5,1,5)     231       7       0     231       7       0  traces/trans.trace
     6 (5,1,5)  265189   21775   21743  265189   21775   21743  traces/long.trace
    27

TEST_CSIM_RESULTS=27
```

## Part A 与 Part B 最终结论

```text
[完成] 官方说明和 PDF 要求阅读
[完成] 目录、编译器、Makefile 与测试程序确认
[完成] Valgrind 安装并验证

[Part A 完成] 参数解析
[Part A 完成] trace 读取
[Part A 完成] I 指令过滤
[Part A 完成] Cache/Set/Line 数据结构
[Part A 完成] 动态分配与初始化
[Part A 完成] 地址 set/tag 拆分
[Part A 完成] hit 判断
[Part A 完成] empty line 填充
[Part A 完成] LRU 替换
[Part A 完成] M 双访问
[Part A 完成] verbose 输出
[Part A 完成] test-csim：27/27

[Part B 完成] 32×32：correctness=1，288 misses
[Part B 完成] 64×64：correctness=1，1220 misses
[Part B 完成] 61×67：correctness=1，1951 misses
```

官方性能阈值：

```text
32×32：misses < 300
64×64：misses < 1300
61×67：misses < 2000
```

本次最终结果：

| 项目 | 正确性 | misses | 官方阈值 | 状态 |
|---|---:|---:|---:|---|
| Part A Cache Simulator | — | — | test-csim 27/27 | 通过 |
| 32×32 transpose | 1 | 288 | < 300 | 通过 |
| 64×64 transpose | 1 | 1220 | < 1300 | 通过 |
| 61×67 transpose | 1 | 1951 | < 2000 | 通过 |

本次 Part A 最终攻坚路径可以概括为：

```text
先用 csim-ref 建立基线
→ 用小 trace 手算状态
→ 分步完成参数解析和 trace 读取
→ 过滤 I
→ 建立 Cache/Set/Line 三层结构
→ 动态分配并初始化 valid/tag/lru
→ 计算 set index 和 tag
→ 区分 hit、空 line miss、满组 eviction
→ 用递增时间戳实现 LRU
→ 抽取 access_cache
→ 将 M 拆成两次访问
→ 对照 csim-ref 和 test-csim
→ 最终 27/27 通过
```

本次 Part B 最终攻坚路径可以概括为：

```text
先让 transpose_submit 正确
→ 测量简单按行扫描基线
→ 32×32 使用 8×8 blocking
→ 推导对角块 A/B set 完全重合
→ 增加 32×32 对角处理，得到 288 misses
→ 测试 64×64 简单 8×8，发现 4612 misses
→ 测试简单 4×4，发现 B/B 自我驱逐，得到 1892 misses
→ 推导 64×64 的 4 行 set 重复
→ 使用 8×8 外块 + 4×4 内部 A12/A21 重排
→ 得到 1220 misses
→ 为 61×67 增加边界安全通用分支
→ 8×8 得到 2119 misses
→ 改用 16×16，得到 1993 misses
→ 再测试 17×17，得到 1951 misses并最终采用
→ 三种矩阵全部达到性能阈值
```

## 复盘后的工程结论

本次实验没有得到一个对所有矩阵都统一的“魔法块大小”，而是得到三种由地址映射规律决定的策略：

```text
32×32：8×8 + 对角块处理
64×64：8×8 外块 + 4×4 内部重排
61×67：17×17 + 边界保护
```

真正需要掌握的不是背下这三个数字，而是遇到新矩阵时能够按照下面的流程重新推导：

```text
确认 Cache block、set 数和相联度
→ 计算矩阵行跨度
→ 推导 A/B 的 set 映射
→ 判断冲突周期和边界碎片
→ 选择候选 tile
→ 先验证 correctness
→ 用 trace/test-trans 测量 miss
→ 只在必要处增加暂存、交换或特殊分支
```

