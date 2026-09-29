#include "cachelab.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>

typedef struct Line
{
    int valid;              // 有效位
    unsigned long long tag; // 标记位
    int lru;                // LRU 计数器（用于淘汰最长时间没有被访问的算法）
} Line;

typedef struct Set
{
    Line *lines; // 指向组内 E 个 Cache Line 的动态数组
} Set;

typedef struct Cache
{
    Set *sets;         // 指向 S = 2^s 个 Set 的动态数组
    int set_count;     // Set 的数量
    int lines_per_set; // 每个 Set 里面行的数量
} Cache;

// 准备参数变量
int s = -1;
int E = -1;
int b = -1;
int verbose = 0;
char *trace_file = NULL;

unsigned long long set_mask = 0; // 全局组掩码

int hit_count = 0;
int miss_count = 0;
int eviction_count = 0;
int lru_count = 0; // 数值越大，表示越晚被访问

Cache cache;

// 将 Cache 访问模拟逻辑抽离成一个函数
void access_cache(unsigned long long address)
{
    lru_count++;

    // 计算地址的 set index 和 tag
    // unsigned long long set_mask = (1 << s) - 1; // 将这条指令放到外面就不用每次循环都计算一次且使用1ULL防止溢出
    unsigned long long set_index = (address >> b) & set_mask;
    unsigned long long tag = address >> (s + b);
    int hit_line = -1;   // 记录命中行，-1 代表未命中
    int empty_line = -1; // 记录找到的第一个空 line，-1 代表没有空 line
    for (int j = 0; j < cache.lines_per_set; j++)
    {
        Line *line = &cache.sets[set_index].lines[j];
        if (line->valid == 1 && line->tag == tag)
        {
            hit_line = j;
            break; // 如果命中的话找没找到空 line无所谓，因为后面用不上
        }
        if (line->valid == 0 && empty_line == -1)
        {
            empty_line = j; // 如果没命中会找到第一个空 line
        }
    }

    // 判断 hit/miss/eviction
    if (hit_line >= 0) // 命中
    {
        hit_count++;
        cache.sets[set_index].lines[hit_line].lru = lru_count;

        if (verbose)
            printf(" hit");
    }
    else
    {
        if (empty_line >= 0) // 未命中但有空行
        {
            miss_count++;
            cache.sets[set_index].lines[empty_line].valid = 1;
            cache.sets[set_index].lines[empty_line].tag = tag;
            cache.sets[set_index].lines[empty_line].lru = lru_count;

            if (verbose)
                printf(" miss");
        }
        else // 未命中且没有空行
        {
            miss_count++;
            eviction_count++;

            // 找最久没被使用的行
            int eviction_line = 0;
            int min_lru = cache.sets[set_index].lines[0].lru;
            for (int j = 1; j < cache.lines_per_set; j++)
            {
                Line *line = &cache.sets[set_index].lines[j];
                if (line->lru < min_lru)
                {
                    min_lru = line->lru;
                    eviction_line = j;
                }
            }
            cache.sets[set_index].lines[eviction_line].valid = 1;
            cache.sets[set_index].lines[eviction_line].tag = tag;
            cache.sets[set_index].lines[eviction_line].lru = lru_count;

            if (verbose)
                printf(" miss eviction");
        }
    }
}

int main(int argc, char **argv)
{
    int option;

    // getopt 循环
    while ((option = getopt(argc, argv, "s:E:b:t:vh")) != -1)
    {
        switch (option)
        {
        case 's':
            s = atoi(optarg);
            break;
        case 'E':
            E = atoi(optarg);
            break;
        case 'b':
            b = atoi(optarg);
            break;
        case 't':
            trace_file = optarg;
            break;
        case 'v':
            verbose = 1;
            break;
        case 'h':
            break;
        default:
            break;
        }
    }

    // 检查必须参数
    if (s == -1 || E == -1 || b == -1 || trace_file == NULL)
    {
        fprintf(stderr, "Missing required command line argument\n");
        return 1; // 1代表异常
    }
    // // 临时打印解析结果
    // printf("parsed: s=%d E=%d b=%d trace_file=%s verbose=%d\n", s, E, b, trace_file, verbose);

    // 初始化 Cache 空间
    cache.set_count = 1 << s;
    cache.lines_per_set = E;
    cache.sets = malloc(sizeof(Set) * cache.set_count);
    if (cache.sets == NULL)
    {
        perror("Error sets malloc fail");
        return 1;
    }

    // 为每个 Set 分配 E 条 Line
    for (int i = 0; i < cache.set_count; i++)
    {
        cache.sets[i].lines = malloc(sizeof(Line) * cache.lines_per_set);
        if (cache.sets[i].lines == NULL)
        {
            perror("Error lines malloc fail");
            return 1;
        }

        // 将每条 Line 进行初始化
        for (int j = 0; j < cache.lines_per_set; j++)
        {
            cache.sets[i].lines[j].valid = 0;
            cache.sets[i].lines[j].tag = 0;
            cache.sets[i].lines[j].lru = 0;
        }
    }

    // 打开文件
    FILE *trace_fp = fopen(trace_file, "r");
    if (trace_fp == NULL)
    {
        perror("Error opening trace file");
        return 1;
    }

    // 准备保存一条记录的变量
    char operation;
    unsigned long long address;
    int size;

    // // 读取一条指令
    // int items = fscanf(trace_fp, " %c %llx,%d", &operation, &address, &size);
    // if (items != 3)
    // {
    //     fprintf(stderr, "Invalid trace format\n");
    //     fclose(trace_fp);
    //     return 1;
    // }
    // // 临时打印指令
    // printf("first record: op=%c address=0x%llx size=%d\n", operation, address, size);

    // 循环读取指令
    set_mask = (1ULL << s) - 1; // 计算全局组掩码
    while (fscanf(trace_fp, " %c %llx,%d", &operation, &address, &size) == 3)
    {
        // 跳过 I 指令
        if (operation == 'I')
        {
            continue;
        }

        if (verbose)
        {
            printf("%c %llx,%d", operation, address, size);
        }

        if (operation == 'L' || operation == 'S')
        {
            access_cache(address);
        }

        // 将 M 指令拆成 L 指令 + S 指令
        if (operation == 'M')
        {
            // 这里并不在乎 Load 和 Store 里面的核心逻辑，只在乎 hit/miss/eviction
            // 且说明了采用写分配策略，所以用了同一个函数
            access_cache(address); // Load
            access_cache(address); // Store, 100% hit
        }

        if (verbose)
            printf("\n");

        // // 临时打印读取的指令
        // if (verbose && record_count <= 5)
        // {
        //     printf("record %d: op=%c address=0x%llx set_index=%llu tag=0x%llx\n", record_count, operation, address, set_index, tag);
        //     if (hit_line >= 0)
        //     {
        //         printf("lookup: hit in line %d\n", hit_line);
        //     }
        //     else
        //     {
        //         printf("lookup: no matching line\n");
        //     }
        // }
    }

    fclose(trace_fp);

    printSummary(hit_count, miss_count, eviction_count);
    return 0;
}
