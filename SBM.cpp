#include "SBM.hpp"
#include <cstdio>
#include <cstring>

bFrame buf[DEFBUFSIZE];
DSMgr dsmgr;  // 全局 DSMgr 实例

DSMgr::DSMgr() {
    currFile = NULL;
    numPages = 0;
    ReadCount = 0;
    WriteCount = 0;
    for (int i = 0; i < MAXPAGES; i++) {
        pages[i] = -1;
    }
}

int DSMgr::OpenFile(string filename){
    if (currFile != NULL) {
        fclose(currFile);
    }
    currFile = fopen(filename.c_str(), "r+");
    if (currFile == NULL) {
        currFile = fopen(filename.c_str(), "w");
        currFile = freopen(filename.c_str(), "r+", currFile);
        numPages = 0;
    }
    else{
        fseek(currFile, 0, SEEK_END);
        long fileSize = ftell(currFile);
        numPages = fileSize / FRAMESIZE;
        for (int i = 0; i < numPages; i++) {
            pages[i] = 1;  // 标记为已使用
        }
    }
    return currFile != NULL ? 0 : -1;
}

int DSMgr::CloseFile(){
    if (currFile == NULL) {
        return -1;
    }
    fclose(currFile);
    currFile = NULL;
    return 0;
}

bFrame DSMgr::ReadPage(int page_id){
    bFrame data;
    if (Seek(page_id * FRAMESIZE, 0) != 0) {
        fprintf(stderr, "Error: cannot find page: %d\n", page_id);
        exit(1);
    }
    fread(data.field, FRAMESIZE, 1, currFile);
    SetUse(page_id, 1);
    ReadCount++;
    return data;
}

int DSMgr::WritePage(int page_id, bFrame frm){
    if (Seek(page_id * FRAMESIZE, 0) != 0) {
        fprintf(stderr, "Error: cannot find page: %d\n", page_id);
        exit(1);
    }
    SetUse(page_id, 0);
    WriteCount++;
    return (int)fwrite(frm.field, FRAMESIZE, 1, currFile);
}

int DSMgr::Seek(int offset, int pos){
    if (pos == 0) {
        return fseek(this->currFile, offset, SEEK_SET);
    } else if (pos == 1) {
        return fseek(this->currFile, offset, SEEK_CUR);
    } else if (pos == 2) {
        return fseek(this->currFile, offset, SEEK_END);
    }
    return -1;
}

FILE *DSMgr::GetFile(){
    return this->currFile;
}

void DSMgr::IncNumPages(){
    this->numPages++;
}

int DSMgr::GetNumPages(){
    return this->numPages;
}

void DSMgr::SetUse(int index, int use_bit){
    this->pages[index] = use_bit;
}

int DSMgr::GetUse(int index){
    return this->pages[index];
}

/*
===============================================
===============================================
*/

BMgr::BMgr(int choice){
    replace_policy = choice;
    memset(this->ftop, -1, sizeof(this->ftop));
    memset(this->ptof, 0, sizeof(this->ptof));
    
    if (replace_policy == 1) {
        // LRU 初始化
        for (int i = 0; i < DEFBUFSIZE; ++i) {
            lru_prev[i] = -1;
            lru_next[i] = -1;
        }
        lru_head = -1;
        lru_tail = -1;
    } else {
        // LRU-2 初始化
        Lsize = 0;
        L2size = 0;
        Lmax = DEFBUFSIZE;
        L2max = 800;
        Lhead = new LRU2Node(-1);
        Ltail = new LRU2Node(-1);
        L2head = new LRU2Node(-1);
        L2tail = new LRU2Node(-1);
        set_pointer(Lhead, Ltail);
        set_pointer(L2head, L2tail);
    }
    
    InBufferCount = 0;
    OutBufferCount = 0;
}

BMgr::~BMgr(){
    if (replace_policy == 2) {
        // 清理 LRU-2 节点（简化处理，实际应该遍历删除所有节点）
        delete Lhead;
        delete Ltail;
        delete L2head;
        delete L2tail;
    }
}

int BMgr::FixPage(int page_id, int prot){
    // 使用 Hash 函数查找 BCB
    BCB *bcb = Hash(page_id);
    
    if (bcb != NULL) {
        // 页面已经在缓冲区中（命中）
        InBufferCount++;
        if (replace_policy == 1) {
            RemoveLRUEle(bcb->frame_id);
            AddLRUEle(bcb->frame_id);
        } else {
            LRU2Fix(bcb->frame_id);
        }
        if (prot) {
            SetDirty(bcb->frame_id);
        }
        return bcb->frame_id;
    } else {
        // 页面不在缓冲区中（未命中）
        OutBufferCount++;
        int frame_id;
        if (NumFreeFrames() > 0) {
            // 有空闲帧，使用第一个空闲帧
            frame_id = DEFBUFSIZE - NumFreeFrames();
        } else {
            // 没有空闲帧，选择 victim
            frame_id = SelectVictim();
        }
        
        // 从磁盘读取页面
        buf[frame_id] = dsmgr.ReadPage(page_id);
        if (replace_policy == 1) {
            AddLRUEle(frame_id);
        } else {
            LRU2Insert(frame_id);
        }
        
        // 创建新的 BCB
        bcb = new BCB();
        bcb->page_id = page_id;
        bcb->frame_id = frame_id;
        bcb->count = 0;
        bcb->latch = 0;
        bcb->dirty = 0;
        bcb->next = NULL;
        
        // 将 BCB 插入哈希表（尾插法）
        int bucket = page_id % DEFBUFSIZE;
        BCB *prebcb = ptof[bucket];
        if (prebcb != NULL) {
            while (prebcb->next != NULL) {
                prebcb = prebcb->next;
            }
            prebcb->next = bcb;
        } else {
            ptof[bucket] = bcb;
        }
        
        ftop[frame_id] = page_id;
        if (prot) {
            SetDirty(bcb->frame_id);
        }
        return frame_id;
    }
} 

NewPage BMgr::FixNewPage(){
    NewPage np;
    np.page_id = -1;
    np.frame_id = -1;

    // 1. 先为新页分配一个 page_id
    int num_pages = dsmgr.GetNumPages();
    int page_id = -1;

    // 1.1 尝试复用已存在但未使用的页（use_bit == 0）
    for (int i = 0; i < num_pages; ++i) {
        if (dsmgr.GetUse(i) == 0) {
            page_id = i;
            break;
        }
    }

    // 1.2 如果没有可复用的页，则在文件末尾分配新页
    if (page_id == -1) {
        page_id = num_pages;
        dsmgr.IncNumPages();
    }
    // 标记该页正在使用
    dsmgr.SetUse(page_id, 1);

    // 2. 为该页找到/分配一个 frame_id
    int frame_id = -1;

    // 2.1 先找一个空闲帧（ftop 中为 -1 的位置）
    for (int i = 0; i < DEFBUFSIZE; ++i) {
        if (ftop[i] == -1) {
            frame_id = i;
            break;
        }
    }

    // 2.2 如果没有空闲帧，则需要选择 victim 进行替换
    if (frame_id == -1) {
        frame_id = SelectVictim();
        if (frame_id == -1) {
            // 无可用帧，返回失败
            return np;
        }

        // 如果 victim 帧中有页面，需要写回 / 清理
        if (ftop[frame_id] != -1) {
            int victim_page_id = ftop[frame_id];
            BCB *victim_bcb = Hash(victim_page_id);
            if (victim_bcb != nullptr) {
                RemoveBCB(victim_bcb, victim_page_id);
            }
            // 从 LRU/LRU-2 中移除
            if (replace_policy == 1) {
                RemoveLRUEle(frame_id);
            }
            // LRU-2 的移除在 RemoveBCB 中处理
        }
    }

    // 3. 在选定的 frame 中初始化新页内容（这里简单清零）
    std::memset(buf[frame_id].field, 0, FRAMESIZE);

    // 4. 为新页创建 BCB，并加入哈希表
    int bucket = page_id % DEFBUFSIZE;
    BCB *new_bcb = new BCB();
    new_bcb->page_id = page_id;
    new_bcb->frame_id = frame_id;
    new_bcb->latch = 0;   // 新页无特殊保护
    new_bcb->count = 1;   // 被 Fix 一次
    new_bcb->dirty = 1;   // 视为脏页，后续需要写回
    new_bcb->next = nullptr;

    // 头插到对应哈希桶链表
    if (ptof[bucket] == nullptr) {
        ptof[bucket] = new_bcb;
    } else {
        new_bcb->next = ptof[bucket];
        ptof[bucket] = new_bcb;
    }

    // 5. 更新 frame_id -> page_id 映射
    ftop[frame_id] = page_id;

    // 6. 加入 LRU/LRU-2 队列
    if (replace_policy == 1) {
        AddLRUEle(frame_id);
    } else {
        LRU2Insert(frame_id);
    }

    np.page_id = page_id;
    np.frame_id = frame_id;
    return np;
}

int BMgr::UnfixPage(int page_id){
    // 根据 page_id 找到对应的 BCB
    BCB *bcb = Hash(page_id);

    // 页面不在缓冲区，无法 Unfix
    if (bcb == nullptr) {
        return -1;
    }

    // 如果当前计数为 0，说明已经没有被 Fix，直接返回
    if (bcb->count == 0) {
        return bcb->frame_id;
    }

    // 1. 递减 fix count
    bcb->count--;

    // 2. 如果计数降为 0，则移除 latch，
    //    这样该页就可以在 LRU 中被 SelectVictim 选中
    if (bcb->count == 0) {
        bcb->latch = 0;
        // LRU 顺序保持不变：仍然用上一次 Fix 时的“最近使用时间”
    }

    return bcb->frame_id;
}

int BMgr::NumFreeFrames(){
    if (replace_policy == 1) {
        // 计算已使用的帧数（在 LRU 中的帧数）
        int used = 0;
        int cur = lru_head;
        while (cur != -1) {
            used++;
            cur = lru_next[cur];
        }
        return DEFBUFSIZE - used;
    } else {
        // LRU-2: 总帧数减去已使用的帧数
        return DEFBUFSIZE - Lsize;
    }
}

int BMgr::SelectVictim(){
    int frame_id;
    if (replace_policy == 1) {
        // LRU 策略：选择最久未使用的帧（LRU 队列头部）
        frame_id = lru_head;
        if (frame_id == -1) {
            return -1;  // 没有可替换的帧
        }
    } else {
        // LRU-2 策略
        frame_id = LRU2Victim();
        if (frame_id == -1) {
            return -1;
        }
    }
    
    int victim_page_id = ftop[frame_id];
    BCB *bcb = Hash(victim_page_id);
    if (bcb != NULL) {
        RemoveBCB(bcb, victim_page_id);
    }
    return frame_id;
}

BCB* BMgr::Hash(int page_id){
    // 静态哈希函数: H(k) = page_id % buffer_size
    int bid = page_id % DEFBUFSIZE;
    BCB *bcb = ptof[bid];
    while (bcb != NULL) {
        if (bcb->page_id == page_id) {
            return bcb;
        }
        bcb = bcb->next;
    }
    return NULL;
}

void BMgr::RemoveBCB(BCB *ptr, int page_id){
    int bid = page_id % DEFBUFSIZE;
    BCB *bcb = ptof[bid];
    if (bcb == ptr) {
        ptof[bid] = ptr->next;
    } else {
        while (bcb->next != ptr) {
            bcb = bcb->next;
        }
        bcb->next = ptr->next;
    }
    if (ptr->dirty) {
        dsmgr.WritePage(ptr->page_id, buf[ptr->frame_id]);
    }
    
    // 从 LRU/LRU-2 中移除
    if (replace_policy == 1) {
        RemoveLRUEle(ptr->frame_id);
    } else {
        // LRU-2: 需要从 L 或 L2 中移除
        int frame_id = ptr->frame_id;
        // 在 L2 中查找
        LRU2Node* node = L2head->next;
        bool found = false;
        while (node != NULL && node->frame_id != -1) {
            if (node->frame_id == frame_id) {
                LRU2Node* nextnode = node->next;
                LRU2Node* prevnode = node->prev;
                set_pointer(prevnode, nextnode);
                delete node;
                L2size--;
                found = true;
                break;
            }
            node = node->next;
        }
        // 如果在 L2 中没找到，在 L 中查找
        if (!found) {
            node = Lhead->next;
            while (node != NULL && node->frame_id != -1) {
                if (node->frame_id == frame_id) {
                    LRU2Node* nextnode = node->next;
                    LRU2Node* prevnode = node->prev;
                    set_pointer(prevnode, nextnode);
                    delete node;
                    Lsize--;
                    break;
                }
                node = node->next;
            }
        }
    }
    
    ftop[ptr->frame_id] = -1;
    delete ptr;
}

// 从 LRU 链表中移除一个 frame
void BMgr::RemoveLRUEle(int frid){
    if (frid < 0 || frid >= DEFBUFSIZE) return;

    int prev = lru_prev[frid];
    int next = lru_next[frid];

    if (prev != -1) {
        lru_next[prev] = next;
    } else {
        // frid 是头结点
        lru_head = next;
    }

    if (next != -1) {
        lru_prev[next] = prev;
    } else {
        // frid 是尾结点
        lru_tail = prev;
    }

    lru_prev[frid] = -1;
    lru_next[frid] = -1;
}

// 将 frame 追加到 LRU 尾部（表示最近使用）
void BMgr::AddLRUEle(int frid){
    if (frid < 0 || frid >= DEFBUFSIZE) return;

    // 如果已经在链表中，先移除
    if (lru_prev[frid] != -1 || lru_next[frid] != -1 || lru_head == frid) {
        RemoveLRUEle(frid);
    }

    if (lru_tail == -1) {
        // 链表为空，frid 既是头又是尾
        lru_head = frid;
        lru_tail = frid;
        lru_prev[frid] = -1;
        lru_next[frid] = -1;
    } else {
        // 插入到尾部
        lru_next[lru_tail] = frid;
        lru_prev[frid] = lru_tail;
        lru_next[frid] = -1;
        lru_tail = frid;
    }
}

// 设置某个 frame 对应页的脏位
void BMgr::SetDirty(int frame_id){
    int page_id = ftop[frame_id];
    BCB *bcb = Hash(page_id);
    if (bcb) {
        bcb->dirty = 1;
    }
}

void BMgr::UnsetDirty(int frame_id){
    int page_id = ftop[frame_id];
    BCB *bcb = Hash(page_id);
    if (bcb) {
        bcb->dirty = 0;
    }
}

void BMgr::WriteDirtys(){
    BCB *bcb;
    for (int i = 0; i < DEFBUFSIZE; i++) {
        if (ftop[i] != -1) {
            bcb = Hash(ftop[i]);
            if (bcb && bcb->dirty) {
                dsmgr.WritePage(bcb->page_id, buf[bcb->frame_id]);
            }
        }
    }
}

void BMgr::PrintFrame(int frame_id){
    if (frame_id < 0 || frame_id >= DEFBUFSIZE) return;
    printf("Frame %d: page_id = %d, latch = %d, count = %d, dirty = %d\n", frame_id, ftop[frame_id], ptof[frame_id]->latch, ptof[frame_id]->count, ptof[frame_id]->dirty);
}

int BMgr::GetIONum(int choice){
    switch (choice) {
        case 0:
            return dsmgr.ReadCount;
        case 1:
            return dsmgr.WriteCount;
        default:
            return dsmgr.ReadCount;
    }
}

// ==================== LRU-2 算法实现 ====================

void BMgr::LRU2Insert(int frame_id) {
    if (Lsize == Lmax) {
        LRU2Remove();
    }
    LRU2Node* node = new LRU2Node(frame_id);
    node->next = Lhead->next;
    if (node->next != NULL) {
        node->next->prev = node;
    }
    set_pointer(Lhead, node);
    Lsize++;
}

void BMgr::LRU2Fix(int frame_id) {
    // 先在 L2（缓存链表）中查找
    LRU2Node* node = L2head->next;
    while (node != NULL && node->frame_id != -1) {
        if (node->frame_id == frame_id) {
            // 找到节点，移到 L2 头部
            LRU2Node* nextnode = node->next;
            LRU2Node* prevnode = node->prev;
            set_pointer(prevnode, nextnode);
            
            node->next = L2head->next;
            if (node->next != NULL) {
                node->next->prev = node;
            }
            set_pointer(L2head, node);
            node->pincount++;
            return;
        }
        node = node->next;
    }
    
    // 在 L2 中没找到，在 L（历史记录链表）中查找
    LRU2Fix2(frame_id);
}

void BMgr::LRU2Fix2(int frame_id) {
    LRU2Node* node = Lhead->next;
    while (node != NULL && node->frame_id != -1) {
        if (node->frame_id == frame_id) {
            // 从 L 中移除
            LRU2Node* nextnode = node->next;
            LRU2Node* prevnode = node->prev;
            set_pointer(prevnode, nextnode);
            Lsize--;
            
            // 添加到 L2 头部
            node->next = L2head->next;
            if (node->next != NULL) {
                node->next->prev = node;
            }
            set_pointer(L2head, node);
            node->pincount++;
            L2size++;
            
            // 如果 L2 超过最大长度，移除尾部
            if (L2size > L2max) {
                LRU2Remove2();
            }
            return;
        }
        node = node->next;
    }
}

int BMgr::LRU2Victim() {
    if (Lsize == 0 && L2size == 0) {
        return -1;
    }
    
    // 优先从 L2 尾部选择，如果 L2 满了或 L 为空，从 L2 尾部选择
    if (L2size >= L2max || Lsize == 0) {
        if (L2tail->prev != NULL && L2tail->prev->frame_id != -1) {
            return L2tail->prev->frame_id;
        }
    }
    
    // 否则从 L 尾部选择
    if (Ltail->prev != NULL && Ltail->prev->frame_id != -1) {
        return Ltail->prev->frame_id;
    }
    
    return -1;
}

void BMgr::LRU2Remove() {
    if (Lsize == 0) return;
    
    LRU2Node* node = Ltail->prev;
    if (node == NULL || node->frame_id == -1) {
        return;
    }
    
    // 如果 L2 满了或 L 尾部是哨兵节点，从 L2 移除
    if (L2size >= L2max || node->frame_id == -1) {
        LRU2Remove2();
        return;
    }
    
    set_pointer(node->prev, Ltail);
    delete node;
    Lsize--;
}

void BMgr::LRU2Remove2() {
    if (L2size == 0) return;
    
    LRU2Node* node = L2tail->prev;
    if (node == NULL || node->frame_id == -1) {
        return;
    }
    
    set_pointer(node->prev, L2tail);
    delete node;
    L2size--;
}