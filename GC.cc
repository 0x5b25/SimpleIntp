#include "GC.h"

#include "Interpreter.h"

void GarbageCollector::ProcessManagedFields(std::stack<std::uint8_t*>& workingSet, ValueType* begin, std::size_t cnt)
{
    //   foreach (refp in object) {
    for (std::size_t i = 0; i < cnt; i++) {
        auto field = begin + i;
        if (!field->type->IsReferenceType()) continue;
        //       old = *refp;
        auto inst = (BytePtr)field->data.obj;
        auto cb = GetCtrlBlk(inst);
        //Do we really needs marking?
        //if (cb->isVisited) continue;
        //cb->isVisited = true;

        //       if (!ptr_in_nursery (old))
        //           continue;
        if(!cb->isInNursery)
        {
            if (cb->isRaw)
                workingSet.push((BytePtr)inst);
            continue;
        }
        

        //       if (object_is_forwarded (old)) {
        //           new = forwarding_destination (old);
        BytePtr newPos;
        if (cb->isForward)
        {
            newPos = (BytePtr)cb->vptr;
        }
        //       } else {
        else {
            //            new = major_alloc (object_size (old));
            //            copy_object (new, old);
            auto gen = cb->generation;
            ManagedObjectCtrlBlock* newInst;
            if(gen >= matureGen)
            {
                newInst = heap.AllocateRaw(cb->objectSize);
                allocMajorHeap += cb->objectSize;
            }
            else
            {
                newInst = m2->AllocateRaw(cb->objectSize);
                newInst->generation = cb->generation + 1;
                allocManaged += cb->objectSize;
            }
            //newInst->vptr = cb->vptr;
            //newInst->isRaw = cb->isRaw;
            (cb->move)((BytePtr)cb, (BytePtr)newInst, cb->objectSize);
            //cb->~ManagedObjectCtrlBlock();
            //            forwarding_set (old, new);
            //cb->vptr = ((BytePtr)newInst) + MOCtrlBlkSize;
            //cb->isForward = true;
            //            gray_stack_push (new);
            if (cb->isRaw)
                workingSet.push((BytePtr)newInst);
            newPos = (BytePtr)newInst + MOCtrlBlkSize;
            //       }
        }
        //       *refp = new;
        field->data.obj = newPos;
    }
}

void GarbageCollector::SweepManaged(const std::vector<ValueType*>& marked)
{
    allocManaged = 0;

    std::stack<std::uint8_t*> workingSet;

    auto _ProcessVT = [&](ValueType& v, bool fromHeap)
    {
        //       old = *refp;
        auto inst = (BytePtr)v.data.obj;
        auto cb = GetCtrlBlk(inst);
        //Do we really needs marking?
        //if (cb->isVisited) continue;
        //cb->isVisited = true;

        //       if (!ptr_in_nursery (old))
        //           continue;
        if (!cb->isInNursery)
        {
            //if (cb->isRaw)
            //    workingSet.push((BytePtr)inst);
            return false;
        }


        //       if (object_is_forwarded (old)) {
        //           new = forwarding_destination (old);
        bool hasBackRef = false;
        BytePtr newPos;
        if (cb->isForward)
        {
            newPos = (BytePtr)cb->vptr;
        }
        //       } else {
        else {
            //            new = major_alloc (object_size (old));
            //            copy_object (new, old);
            auto gen = cb->generation;
            ManagedObjectCtrlBlock* newInst;
            if (gen >= matureGen)
            {
                newInst = heap.AllocateRaw(cb->objectSize);
                allocMajorHeap += cb->objectSize;
            }
            else
            {
                newInst = m2->AllocateRaw(cb->objectSize);
                newInst->generation = cb->generation + 1;
                allocManaged += cb->objectSize;
            }
            //newInst->vptr = cb->vptr;
            //newInst->isRaw = cb->isRaw;
            (cb->move)((BytePtr)cb, (BytePtr)newInst, cb->objectSize);
            //cb->~ManagedObjectCtrlBlock();
            //            forwarding_set (old, new);
            //cb->vptr = ((BytePtr)newInst) + MOCtrlBlkSize;
            //cb->isForward = true;
            //            gray_stack_push (new);
            if (cb->isRaw)
                workingSet.push((BytePtr)newInst);
            newPos = (BytePtr)newInst + MOCtrlBlkSize;
            //       }
        }
        //       *refp = new;
        if(fromHeap && GetCtrlBlk(newPos)->isInNursery)
        {
            backwardRefs.insert(&v);
            hasBackRef = true;
        }
        v.data.obj = newPos;
        return hasBackRef;
    };

    //ProcessManagedFields(workingSet, marked.data(), marked.size());
    for(auto v : marked)
    {
        assert(!v->IsNull());
        //Static fields are allocated on heap, so
        //not all objects are of reference type
        //assert(v->IsRef());
        //assert(!GetCtrlBlk(v->data.obj)->isForward);
        _ProcessVT(*v, false);
    }

    //Process backward refs, that is Heap->Nursery
    auto rit = backwardRefs.begin();
    while(rit != backwardRefs.end())
    {
        auto vp = *rit;
        //remove empty back refs, possibly because nursery objects
        //are moved into heap
        if(_ProcessVT(*vp, true))
            rit++;
        else
            rit = backwardRefs.erase(rit);
    }

    //process along the reference graph
    while (!workingSet.empty())
    {
        //   object = gray_stack_pop ();
        auto base = workingSet.top();
        workingSet.pop();
        auto objCB = (ManagedObjectCtrlBlock*)(base);
        auto payload = (ValueType*)(base + MOCtrlBlkSize);
        auto fieldCnt = objCB->objectSize / sizeof(ValueType);
        for (std::size_t i = 0; i < fieldCnt; i++) {
            auto field = payload + i;
            if (!field->IsRef()) continue;

            _ProcessVT(*field,!objCB->isInNursery);
        }
        //ProcessManagedFields(workingSet, payload, fieldCnt);        
    }

    //Finalizing pass
    for (auto blk : m1->blks)
    {
        auto base = (BytePtr)blk;
        base += MBCtrlBlkSize;
        std::size_t currSize = 0;
        while (currSize < blk->usedBytes)
        {
            auto objCB = (ManagedObjectCtrlBlock*)(base + currSize);
            //auto payload = GetPayload(objCB);
            currSize += MOCtrlBlkSize + objCB->objectSize;
    
            if (!objCB->isForward && !objCB->isRaw)
            {
                objCB->dtor((BytePtr)objCB);
            }
        }

        blk->usedBytes = 0;
        blk->objectCnt = 0;
    }

    m1->inUseBlkCnt = 0;


    SwapActiveMM();
}

void GarbageCollector::SweepMajorHeap(const std::vector<ValueType*>& marked)
{
    allocMajorHeap = 0;
    backwardRefs.clear();
    {
        //Clear visit flags
        auto currNode = heap.GetFirstNode();
        while (currNode!=nullptr)
        {
            auto cb = GetCtrlBlkFromNode(currNode);
            cb->isVisited = false;
            currNode = MajorHeap::GetNextNode(currNode);
        }
        for (auto blk : m1->blks)
        {
            auto base = (BytePtr)blk;
            base += MBCtrlBlkSize;
            std::size_t currSize = 0;
            while (currSize < blk->usedBytes)
            {
                auto objCB = (ManagedObjectCtrlBlock*)(base + currSize);
                currSize += MOCtrlBlkSize + objCB->objectSize;
                objCB->isVisited = false;
            }
        }
    }
    //Mark objects
    std::stack<ManagedObjectCtrlBlock*> workingSet;
    for(auto v:marked)
    {
        //assert(v->IsRef());
        auto cb = GetCtrlBlk(v->data.obj);
        if(cb->isVisited) continue;

        cb->isVisited = true;

        if (!cb->isInNursery)
            allocMajorHeap += cb->objectSize;
        if(cb->isRaw)
            workingSet.push(GetCtrlBlk(v->data.obj));
    }
    while (!workingSet.empty())
    {
        auto objCB = workingSet.top();
        workingSet.pop();
        assert(!objCB->isForward);
                
        auto payload = (ValueType*)GetPayload(objCB);
        auto fieldCnt = objCB->objectSize / sizeof(ValueType);
        for (std::size_t i = 0; i < fieldCnt; i++) {
            auto field = payload + i;
            if(!field->IsRef()) continue;

            auto fieldCB = GetCtrlBlk(field->data.obj);
            if(fieldCB->isVisited) continue;

            fieldCB->isVisited = true;

            if(fieldCB->isInNursery)
            {
                if(!objCB->isInNursery)
                    backwardRefs.insert(field);
            }else
            {
                allocMajorHeap += fieldCB->objectSize;
            }
            if (fieldCB->isRaw)
                workingSet.push(fieldCB);
        }
    }

    //Destroy not visited objects
    {
        //Clear visit flags
        auto currNode = heap.GetFirstNode();
        auto prevNode = heap.GetHeaderNode();
        while (currNode != nullptr)
        {
            auto cb = GetCtrlBlkFromNode(currNode);
            if(!cb->isVisited)
            {
                heap.RemoveNode(currNode, prevNode);
                currNode = MajorHeap::GetNextNode(prevNode);
                continue;
            }

            prevNode = currNode;
            currNode = MajorHeap::GetNextNode(currNode);
        }
    }
}

ValueType* GarbageCollector::AllocateRawObject(std::size_t fieldCnt)
{
    auto blk = m1->AllocateRaw(sizeof(ValueType) * fieldCnt);
    auto payload = GetPayload(blk);
    allocManaged += blk->objectSize;
    return (ValueType*)payload;
}

bool GarbageCollector::WriteField(const ValueType& src, const ValueType& dst, std::size_t idx)
{
    if(!dst.IsRef()) return false;
    auto inst = (ValueType*)dst.data.obj;
    auto dst_cb = GetCtrlBlk(inst);
    if(!dst_cb->isRaw) return false;
    auto fieldCnt = dst_cb->objectSize / sizeof(ValueType);
    if(idx >= fieldCnt) return false;

    //assert(inst[idx].type == src.type);
    inst[idx].data = src.data;

    if(!src.IsRef()) return true;

    auto src_cb = GetCtrlBlk(src.data.obj);

    if(src_cb->isInNursery && !dst_cb->isInNursery)
    {
        //Backward cross reference from heap to nursery
        backwardRefs.insert(inst + idx);
    }

    return true;
}

std::string GarbageCollector::PrintAllocStat()
{
    std::string msg;
    int newCnt = 0;
    for (auto blk : m1->blks)
    {
        newCnt += blk->objectCnt;
    }

    msg += "============New Alloc============\n";
    msg += " size:  " + std::to_string(allocManaged) + " bytes" + '\n';
    msg += " count: " + std::to_string(newCnt) + '\n';
    msg += "---------------------------------\n";
    int idx = 0;
    for (auto blk : m1->blks)
    {
        auto base = (BytePtr)blk;
        base += MBCtrlBlkSize;
        std::size_t currSize = 0;
        while (currSize < blk->usedBytes)
        {
            auto objCB = (ManagedObjectCtrlBlock*)(base + currSize);
            currSize += MOCtrlBlkSize + objCB->objectSize;
            //auto ty = ((TypeTable*)objCB->vptr);
            //msg += " [" + (ty == nullptr ? "???" : ty->name) + "]";
            msg += " [" + std::to_string(idx++) + "]";
            msg += " : " + std::to_string(objCB->objectSize) + " bytes" + "\n";
        }
    }
    msg += "=================================\n";

    msg += "===========Heap Alloc============\n";
    msg += " size:  " + std::to_string(allocMajorHeap) + " bytes" + "\n";
    msg += " count: " + std::to_string(heap.NodeCount()) + "\n";
    msg += "---------------------------------\n";

    auto n = heap.GetFirstNode();
    idx = 0;
    while (n!= nullptr)
    {
        auto objCB = GetCtrlBlkFromNode(n);
        //auto ty = ((TypeTable*)objCB->vptr);
        //msg += " [" + (ty == nullptr ? "???" : ty->name) + "]";
        msg += " [" + std::to_string(idx++) + "]";
        msg += " : " + std::to_string(objCB->objectSize) + " bytes" + "\n";
        n = MajorHeap::GetNextNode(n);
    }

    //msg += "---------------------------------\n";
    msg += "=================================\n";

    return msg;
}

