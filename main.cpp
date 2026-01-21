#include "SBM.hpp"
#include <iostream>
#include <cstdio>
#include <ctime>
#include <cstring>
using namespace std;

// 声明全局 dsmgr（在 SBM.cpp 中定义）
extern DSMgr dsmgr;

int main(){
    // 1. 打开或创建数据库文件
    FILE* db_file = fopen("data.dbf", "r");
    if (db_file == NULL) {
        // 文件不存在，需要创建
        db_file = fopen("data.dbf", "w");
        if (db_file == NULL) {
            fprintf(stderr, "Error: cannot create data.dbf\n");
            return -1;
        }
        // 创建5万个空页面
        void* buffer = malloc(50000 * FRAMESIZE);
        memset(buffer, 0, 50000 * FRAMESIZE);
        fwrite(buffer, FRAMESIZE, 50000, db_file);
        free(buffer);
        fclose(db_file);
        printf("成功创建数据库文件 data.dbf (50000 页)\n");
    } else {
        fclose(db_file);
        printf("数据库文件 data.dbf 已存在\n");
    }
    
    // 2. 初始化 Buffer Manager
    clock_t start_time = clock();
    int choice;
    printf("输入1选择LRU算法，输入2选择LRU-2算法：");
    scanf("%d", &choice);
    if (choice == 2) {
        printf("LRU-2算法设定缓存链表长度为800\n");
    }
    BMgr bmgr(choice);
    
    // 3. 打开数据库文件
    if (dsmgr.OpenFile("data.dbf") != 0) {
        fprintf(stderr, "Error: cannot open data.dbf\n");
        return -1;
    }
    
    // 4. 如果文件是新创建的，使用 FixNewPage 创建5万个页面
    // 注意：如果文件已存在且已有5万页，这一步可以跳过
    // 但为了确保所有页面都被正确初始化，我们仍然执行
    printf("初始化页面...\n");
    for (int i = 0; i < 50000; i++){
        NewPage np = bmgr.FixNewPage();
        if (np.page_id == -1){
            cout << "FixNewPage failed at page " << i << endl;
            return -1;
        }
        // 取消固定页面
        bmgr.UnfixPage(np.page_id);
    }
    printf("页面初始化完成\n");
    
    // 5. 将所有脏页写回磁盘
    bmgr.WriteDirtys();
    
    // 6. 重置统计计数（因为初始化过程会产生IO）
    dsmgr.ReadCount = 0;
    dsmgr.WriteCount = 0;
    bmgr.InBufferCount = 0;
    bmgr.OutBufferCount = 0;
    
    // 7. 读取 trace 文件并执行请求
    printf("开始处理 trace 文件...\n");
    FILE* trace_file = fopen("data-5w-50w-zipf.txt", "r");
    if (trace_file == NULL) {
        fprintf(stderr, "Error: file data-5w-50w-zipf.txt doesn't exist\n");
        return -1;
    }
    
    int is_dirty, page_id;
    int line_count = 0;
    while (fscanf(trace_file, "%d,%d", &is_dirty, &page_id) != EOF) {
        bmgr.FixPage(page_id, is_dirty);
        // 注意：参考实现中没有调用 UnfixPage，所以页面会一直保持在缓冲区
        // bmgr.UnfixPage(page_id);
        line_count++;
        if (line_count % 50000 == 0) {
            printf("已处理 %d 条请求...\n", line_count);
        }
    }
    fclose(trace_file);
    
    // 8. 写回所有脏页
    bmgr.WriteDirtys();
    
    // 9. 关闭文件
    dsmgr.CloseFile();
    
    // 10. 输出统计结果
    clock_t end_time = clock();
    double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    
    printf("\n========== 统计结果 ==========\n");
    printf("命中计数: %d\n", bmgr.InBufferCount);
    printf("未命中计数: %d\n", bmgr.OutBufferCount);
    int total_requests = bmgr.InBufferCount + bmgr.OutBufferCount;
    if (total_requests > 0) {
        double hit_rate = (double)bmgr.InBufferCount * 100.0 / total_requests;
        printf("命中率: %.2f%%\n", hit_rate);
    }
    printf("读页次数: %d\n", bmgr.GetIONum(0));
    printf("写页次数: %d\n", bmgr.GetIONum(1));
    printf("IO总次数: %d\n", bmgr.GetIONum(0) + bmgr.GetIONum(1));
    printf("运行时间: %.2fs\n", elapsed_time);
    printf("=============================\n");
    
    return 0;
}