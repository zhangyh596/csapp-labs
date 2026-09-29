/*
 * trans.c - Matrix transpose B = A^T
 *
 * Each transpose function must have a prototype of the form:
 * void trans(int M, int N, int A[N][M], int B[M][N]);
 *
 * A transpose function is evaluated by counting the number of misses
 * on a 1KB direct mapped cache with a block size of 32 bytes.
 */
#include <stdio.h>
#include "cachelab.h"

int is_transpose(int M, int N, int A[N][M], int B[M][N]);

/*
 * transpose_submit - This is the solution transpose function that you
 *     will be graded on for Part B of the assignment. Do not change
 *     the description string "Transpose submission", as the driver
 *     searches for that string to identify the transpose function to
 *     be graded.
 */
char transpose_submit_desc[] = "Transpose submission";
void transpose_submit(int M, int N, int A[N][M], int B[M][N])
{
    if (M == 32 && N == 32)
    {
        // 第一关：针对 32*32 矩阵，将矩阵进行 8*8 分块
        // 外层两重循环：以 8 为步长锁定 8*8 的 Tile 小方块
        for (int block_row = 0; block_row < N; block_row += 8)
        {
            for (int block_col = 0; block_col < M; block_col += 8)
            {
                // 对角块极容易发生冲突不命中，进行特殊处理
                if (block_row == block_col)
                {
                    for (int i = block_row; i < block_row + 8; i++)
                    {
                        int v0 = A[i][block_col];
                        int v1 = A[i][block_col + 1];
                        int v2 = A[i][block_col + 2];
                        int v3 = A[i][block_col + 3];
                        int v4 = A[i][block_col + 4];
                        int v5 = A[i][block_col + 5];
                        int v6 = A[i][block_col + 6];
                        int v7 = A[i][block_col + 7];

                        B[block_col][i] = v0;
                        B[block_col + 1][i] = v1;
                        B[block_col + 2][i] = v2;
                        B[block_col + 3][i] = v3;
                        B[block_col + 4][i] = v4;
                        B[block_col + 5][i] = v5;
                        B[block_col + 6][i] = v6;
                        B[block_col + 7][i] = v7;
                    }
                }
                else
                {
                    // 内层两重循环：在 8*8 方块内部逐个元素转置
                    for (int i = block_row; i < block_row + 8; i++)
                    {
                        for (int j = block_col; j < block_col + 8; j++)
                        {
                            int tmp = A[i][j];
                            B[j][i] = tmp;
                        }
                    }
                }
            }
        }
    }
    else if (M == 64 && N == 64)
    {
        // 第二关：针对 64*64 矩阵，8*8 宏观分块 + 4 步寄存器中转换位（避开 4 行冲突死线）
        int v0, v1, v2, v3, v4, v5, v6, v7;
        for (int block_row = 0; block_row < N; block_row += 8)
        {
            for (int block_col = 0; block_col < M; block_col += 8)
            {
                // 阶段一：处理 A 的前四行
                for (int i = block_row; i < block_row + 4; i++)
                {
                    // 直接读出 A 一行里面的 8 个元素
                    v0 = A[i][block_col];
                    v1 = A[i][block_col + 1];
                    v2 = A[i][block_col + 2];
                    v3 = A[i][block_col + 3];
                    v4 = A[i][block_col + 4];
                    v5 = A[i][block_col + 5];
                    v6 = A[i][block_col + 6];
                    v7 = A[i][block_col + 7];

                    // 将 A_topL 转置写入 B_topL (B 的第 block_col ~ block_col+3 行)
                    B[block_col][i] = v0;
                    B[block_col + 1][i] = v1;
                    B[block_col + 2][i] = v2;
                    B[block_col + 3][i] = v3;

                    // 将 A_topR 转置临时暂存到 B_topR (仍保持在 B 的第 block_col ~ block_col+3 行)
                    B[block_col][i + 4] = v4;
                    B[block_col + 1][i + 4] = v5;
                    B[block_col + 2][i + 4] = v6;
                    B[block_col + 3][i + 4] = v7;
                }

                // 阶段二：处理 A_botL 并完成交叉换位归位
                for (int j = 0; j < 4; j++)
                {
                    // 从 B_topR 中把刚才临时存放的 A_topRᵀ 对应数据“抢救”出来 (j 控制 B 的行偏移 0 ~ 3)
                    // 先从 B_topR 读出暂存的 A_topRᵀ 可以减少后续从 A_botL 读取时发生 A/B 冲突不命中
                    v4 = B[block_col + j][block_row + 4];
                    v5 = B[block_col + j][block_row + 5];
                    v6 = B[block_col + j][block_row + 6];
                    v7 = B[block_col + j][block_row + 7];

                    // 从 A_botL 读取一列(A[block_row+4 ~ block_row+7][block_col+j]) (j 控制 A 的列偏移 0 ~ 3)
                    v0 = A[block_row + 4][block_col + j];
                    v1 = A[block_row + 5][block_col + j];
                    v2 = A[block_row + 6][block_col + j];
                    v3 = A[block_row + 7][block_col + j];

                    // 将从 A_botL 读出的 v0~v3 写入 B_topR 真正的归位位置 (j 控制 B 的行偏移 0 ~ 3)
                    B[block_col + j][block_row + 4] = v0;
                    B[block_col + j][block_row + 5] = v1;
                    B[block_col + j][block_row + 6] = v2;
                    B[block_col + j][block_row + 7] = v3;

                    // 将从 B_topR 抢救出的 v4~v7 (A_topRᵀ) 写入真正的目的地 B_botL
                    B[block_col + 4 + j][block_row] = v4;
                    B[block_col + 4 + j][block_row + 1] = v5;
                    B[block_col + 4 + j][block_row + 2] = v6;
                    B[block_col + 4 + j][block_row + 3] = v7;
                }

                // 阶段三：处理 A_botR (右下角 4x4 方块)
                for (int j = 0; j < 4; j++)
                {
                    // 读取 A_botR 的一列 (j 控制列偏移 0 ~ 3)
                    v0 = A[block_row + 4][block_col + 4 + j];
                    v1 = A[block_row + 5][block_col + 4 + j];
                    v2 = A[block_row + 6][block_col + 4 + j];
                    v3 = A[block_row + 7][block_col + 4 + j];

                    // 将从 A_botR 读出的 v0~v3 写入 B_botR 的相应位置
                    B[block_col + 4 + j][block_row + 4] = v0;
                    B[block_col + 4 + j][block_row + 5] = v1;
                    B[block_col + 4 + j][block_row + 6] = v2;
                    B[block_col + 4 + j][block_row + 7] = v3;
                }
            }
        }
    }
    else
    {
        // 首先尝试 8*8 分块
        // misses 次数在 2119，不符合要求
        // 后面尝试改成 16*16 分块
        for (int block_row = 0; block_row < N; block_row += 16)
        {
            for (int block_col = 0; block_col < M; block_col += 16)
            {
                for (int i = block_row; i < block_row + 16 && i < N; i++)
                {
                    for (int j = block_col; j < block_col + 16 && j < M; j++)
                    {
                        int tmp = A[i][j];
                        B[j][i] = tmp;
                    }
                }
            }
        }
    }
}

void transpose_64x64(int M, int N, int A[N][M], int B[M][N])
{
    // 针对 64*64 矩阵，将矩阵进行 4*4 分块
    // 外层两重循环：以 4 为步长锁定 4*4 的 Tile 小方块
    for (int block_row = 0; block_row < N; block_row += 4)
    {
        for (int block_col = 0; block_col < M; block_col += 4)
        {
            // 内层两重循环：在 4*4 方块内部逐个元素转置
            for (int i = block_row; i < block_row + 4; i++)
            {
                for (int j = block_col; j < block_col + 4; j++)
                {
                    int tmp = A[i][j];
                    B[j][i] = tmp;
                }
            }
        }
    }
    // 但是这个方法仍然不能达到要求需要继续优化
}

/*
 * You can define additional transpose functions below. We've defined
 * a simple one below to help you get started.
 */

/*
 * trans - A simple baseline transpose function, not optimized for the cache.
 */
char trans_desc[] = "Simple row-wise scan transpose";
void trans(int M, int N, int A[N][M], int B[M][N])
{
    int i, j, tmp;

    for (i = 0; i < N; i++)
    {
        for (j = 0; j < M; j++)
        {
            tmp = A[i][j];
            B[j][i] = tmp;
        }
    }
}

/*
 * registerFunctions - This function registers your transpose
 *     functions with the driver.  At runtime, the driver will
 *     evaluate each of the registered functions and summarize their
 *     performance. This is a handy way to experiment with different
 *     transpose strategies.
 */
void registerFunctions()
{
    /* Register your solution function */
    registerTransFunction(transpose_submit, transpose_submit_desc);

    /* Register any additional transpose functions */
    registerTransFunction(trans, trans_desc);
}

/*
 * is_transpose - This helper function checks if B is the transpose of
 *     A. You can check the correctness of your transpose by calling
 *     it before returning from the transpose function.
 */
int is_transpose(int M, int N, int A[N][M], int B[M][N])
{
    int i, j;

    for (i = 0; i < N; i++)
    {
        for (j = 0; j < M; ++j)
        {
            if (A[i][j] != B[j][i])
            {
                return 0;
            }
        }
    }
    return 1;
}
