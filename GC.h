#pragma once

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <queue>
#include <stack>
#include <string>
#include <unordered_set>

using BytePtr = std::uint8_t*;

#define PtrSize (sizeof(void*))
#define NHeaderSize (sizeof(NodeHeader))
#define MBCtrlBlkSize (sizeof(ManagedMemCtrlBlock))
#define MOCtrlBlkSize (sizeof(ManagedObjectCtrlBlock))

#define GetCtrlBlk(obj) ((ManagedObjectCtrlBlock*)((BytePtr)obj - MOCtrlBlkSize))
#define GetPayload(cb) ((BytePtr)cb + MOCtrlBlkSize)

#define GetCtrlBlkFromNode(n) ((ManagedObjectCtrlBlock*)((BytePtr)n + PtrSize))


class Interpreter;
class GarbageCollector;
class ValueType;


struct ManagedObjectCtrlBlock
{
    std::uint32_t objectSize;
    std::string debugInfo;

    //GC Flags:
    bool isVisited;
    bool isForward;
    bool isRaw;
    bool isInNursery;
    char generation;

    /**
     * \brief points to object typetable, or
     * forwarded position after GC
     */
    void* vptr;

    //Manage functions
    void(*move)(BytePtr src, BytePtr dst, std::size_t size);
    void(*dtor)(BytePtr ptr);

    std::size_t TotalBytes() const {return MOCtrlBlkSize + objectSize;}

    ~ManagedObjectCtrlBlock()
    {
        dtor((BytePtr)this);
    }

    /*********************************************************************/
    /*                          Object allocation                        */
    /*********************************************************************/

    //Must have at least enough space for ctrlblk
    static ManagedObjectCtrlBlock* EmplaceRaw(void* ptr, int objSize)
    {
        auto ctrl = new(ptr)ManagedObjectCtrlBlock();
        ctrl->isVisited = false;
        ctrl->isForward = false;
        ctrl->isRaw = true;
        ctrl->generation = 0;
        ctrl->objectSize = objSize;
        ctrl->dtor = [](BytePtr ptr) {};
        ctrl->move = [](BytePtr src, BytePtr dst, std::size_t size)
        {
            memcpy(dst + MOCtrlBlkSize, src + MOCtrlBlkSize, size);
            auto newInst = (ManagedObjectCtrlBlock*)dst;
            auto cb = (ManagedObjectCtrlBlock*)src;
            newInst->vptr = cb->vptr;
            newInst->isRaw = cb->isRaw;
            newInst->debugInfo = cb->debugInfo;
            //newInst->generation = cb->generation + 1;
            //(cb->move)((BytePtr)cb, (BytePtr)newInst, cb->objectSize);
            cb->~ManagedObjectCtrlBlock();
            //            forwarding_set (old, new);
            cb->vptr = ((BytePtr)newInst) + MOCtrlBlkSize;
            cb->isForward = true;
        };

        return ctrl;
    }

    template<typename T, typename ...ArgTypes>
    static ManagedObjectCtrlBlock* EmplaceType(void* ptr, ArgTypes... args)
    {
        auto payload = (BytePtr)ptr + MOCtrlBlkSize;
        /*T* obj = */new(payload)T(args...);

        auto ctrl = new(ptr)ManagedObjectCtrlBlock();
        ctrl->isVisited = false;
        ctrl->isForward = false;
        ctrl->isRaw = false;
        ctrl->generation = 0;
        ctrl->objectSize = sizeof(T);
        ctrl->dtor = [](BytePtr ptr) {((T*)((BytePtr)ptr + MOCtrlBlkSize))->~T(); };
        ctrl->move = [](BytePtr src, BytePtr dst, std::size_t size)
        {
            new(dst + MOCtrlBlkSize)T(std::move(*((T*)(src + MOCtrlBlkSize))));
            auto newInst = (ManagedObjectCtrlBlock*)dst;
            auto cb = (ManagedObjectCtrlBlock*)src;
            newInst->vptr = cb->vptr;
            newInst->isRaw = cb->isRaw;
            newInst->debugInfo = cb->debugInfo;
            //newInst->generation = cb->generation + 1;
            //(cb->move)((BytePtr)cb, (BytePtr)newInst, cb->objectSize);
            cb->~ManagedObjectCtrlBlock();
            //            forwarding_set (old, new);
            cb->vptr = ((BytePtr)newInst) + MOCtrlBlkSize;
            cb->isForward = true;
        };

        return ctrl;
    }

};

struct ManagedMemCtrlBlock
{
    std::size_t totalBytes, usedBytes, objectCnt;
    std::size_t Available() const{return totalBytes - usedBytes;}
};

class ManagedNursery
{
    friend class GarbageCollector;
    /*********************************************************************/
    /*                          Block management                         */
    /*********************************************************************/
    ManagedMemCtrlBlock* NewManagedBlk(std::size_t size)
    {
        void* newBlk = malloc(size + MBCtrlBlkSize);

        auto begin = (ManagedMemCtrlBlock*)newBlk;

        ManagedMemCtrlBlock* ctrl = new(begin)ManagedMemCtrlBlock();
        ctrl->totalBytes = size;
        ctrl->usedBytes = 0;
        ctrl->objectCnt = 0;

        return begin;
    }

    void DestroyManagedBlk(ManagedMemCtrlBlock* blk)
    {
        blk->~ManagedMemCtrlBlock();
        free(blk);
    }

    std::vector<ManagedMemCtrlBlock*> blks;

    std::size_t inUseBlkCnt = 0;
    
    ManagedMemCtrlBlock* GetAvailableBlock(std::size_t size)
    {
        //Try last one
        if(inUseBlkCnt > 0)
        {
            auto blk = blks[inUseBlkCnt - 1];
            if(blk->Available() >= size) return blk;
        }

        //Last one is full/not existed
        //Try cached ones
        if(inUseBlkCnt < blks.size())
        {
            auto blk = blks[inUseBlkCnt++];
            return blk;
        }

        //Not enough cache, allocate new one
        
        {
            auto blk = NewManagedBlk(blockSizeInBytes);
            blks.emplace_back(blk);
            inUseBlkCnt++;
            return blk;
        }
    }

    void* AllocateFromManaged(std::size_t size)
    {
        assert(size < blockSizeInBytes + MBCtrlBlkSize);
        //Allocate from the last one
        auto blk = GetAvailableBlock(size);
        assert(blk->Available() >= size);
        auto payload = ((std::uint8_t*)blk);
        //Move to available space
        payload += MBCtrlBlkSize + blk->usedBytes;
        //Register usage
        blk->usedBytes += size;
        blk->objectCnt++;
        return payload;
    }


public:

    std::size_t blockSizeInBytes = 4 << 20; //Default 4MB chunks

    ManagedObjectCtrlBlock* AllocateRaw(std::size_t size)
    {
        auto totalSize = size +MOCtrlBlkSize;
        auto space = AllocateFromManaged(totalSize);
        auto cb = ManagedObjectCtrlBlock::EmplaceRaw(space, size);
        cb->isInNursery = true;
        return cb;
    }

    template<typename T, typename ...ArgTypes>
    ManagedObjectCtrlBlock* Allocate(ArgTypes... args)
    {
        auto totalSize = sizeof(T) + MOCtrlBlkSize;
        auto space = AllocateFromManaged(totalSize);
        auto cb = ManagedObjectCtrlBlock::EmplaceType<T, ArgTypes...>(space, args...);
        cb->isInNursery = true;
        return cb;
    }

    ~ManagedNursery()
    {
        for(auto blk : blks)
        {
            free(blk);
        }
    }
};

/* Long lived object heap
 * +-------------------+
 * | Next ptr          |
 * +-------------------+
 * | Object ctrl block |
 * +-------------------+
 * | payload           |
 *   ...
 */
struct NodeHeader {
    NodeHeader* next;
};
class MajorHeap
{
    NodeHeader* front, *back;
    NodeHeader headerNode;
    int nodeCnt = 0;

    void* AllocateOnHeap(std::size_t size)
    {
        std::size_t totalSize = MOCtrlBlkSize + NHeaderSize + size;
        auto node = (NodeHeader*)malloc(totalSize);
        //auto nextNodePtr = node;
        node->next = nullptr;

        //Add to chain
        back->next = node;
        back = node;
        nodeCnt ++;
        return (BytePtr)node + NHeaderSize;
    }

public:
    int NodeCount() const {return nodeCnt;}

    MajorHeap()
    {
        headerNode.next = nullptr;
        front = back = &headerNode;
    }

    ~MajorHeap()
    {
        auto currNode = GetFirstNode();
        //void* prevNode = *front;
        while (currNode != nullptr)
        {
            auto cb = (ManagedObjectCtrlBlock*)((BytePtr)currNode + NHeaderSize);
            cb->~ManagedObjectCtrlBlock();


            auto nextNode = GetNextNode(currNode);
            free(currNode);
            currNode = nextNode;
        }
    }

    ManagedObjectCtrlBlock* AllocateRaw(std::size_t size)
    {
        auto totalSize = size + MOCtrlBlkSize;
        auto space = AllocateOnHeap(totalSize);
        auto cb = ManagedObjectCtrlBlock::EmplaceRaw(space, size);
        cb->isInNursery = false;
        return cb;
    }

    template<typename T, typename ...ArgTypes>
    ManagedObjectCtrlBlock* Allocate(ArgTypes... args)
    {
        auto totalSize = sizeof(T) + MOCtrlBlkSize;
        auto space = AllocateOnHeap(totalSize);
        auto cb = ManagedObjectCtrlBlock::EmplaceType<T, ArgTypes...>(space, args...);
        cb->isInNursery = false;
        return cb;
    }

    NodeHeader* GetHeaderNode() const {return front;}

    NodeHeader* GetFirstNode() const { return GetNextNode(front); }
    void RemoveNode(NodeHeader* thisNode, NodeHeader* prevNode)
    {
        assert(thisNode != &headerNode);
        SetNextNode(prevNode, thisNode->next);
        nodeCnt--;
        if(thisNode == back)
            back = prevNode;

        free(thisNode);
    }

    static NodeHeader* GetNextNode(NodeHeader* thisNode) { return thisNode->next; }
    static void SetNextNode(NodeHeader* thisNode, NodeHeader* nextNode){
        thisNode->next = nextNode;
    }
};

class GarbageCollector
{
    //Activated, backup
    std::unique_ptr<ManagedNursery> m1, m2;

    //Long lived objects goes here
    MajorHeap heap;
    

    void SwapActiveMM()
    {
        std::swap(m1, m2);
    }

    void ProcessManagedFields(std::stack<std::uint8_t*>& workingSet, ValueType* begin, std::size_t cnt);
    

public:
    std::unordered_set<ValueType*> backwardRefs;
    //An object is considered mature after 4 GCs
    int matureGen = 4;

    int allocManaged = 0, allocMajorHeap = 0;

    GarbageCollector()
        :m1(std::make_unique<ManagedNursery>())
        ,m2(std::make_unique<ManagedNursery>())
    {}
    ~GarbageCollector(){}

    //Only mark those in nursery
    void SweepManaged(const std::vector<ValueType*>& marked);

    //full sweep, also reconstruct backward list
    void SweepMajorHeap(const std::vector<ValueType*>& marked);

    ValueType* AllocateRawObject(std::size_t fieldCnt);

    template<typename T, typename ...ArgTypes>
    T* AllocateObject(ArgTypes... args)
    {
        auto blk = m1->Allocate<T, ArgTypes...>(args...);
        auto payload = GetPayload(blk);
        allocManaged += blk->objectSize;
        return (T*)payload;
    }

    bool WriteField(const ValueType& src, const ValueType& dst, std::size_t idx);

    std::string PrintAllocStat();
};

