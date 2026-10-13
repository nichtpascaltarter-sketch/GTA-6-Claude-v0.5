// Command context: slot bindings resolved into pipeline states, descriptor tables and root parameters at each
// draw / dispatch, with automatic resource transitions. Included from gfx.cpp.
namespace gfx {
namespace {

const D3D12_RESOURCE_STATES kSrvStateDirect = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
const D3D12_RESOURCE_STATES kGraphicsOnlyStates =
    D3D12_RESOURCE_STATE_INDEX_BUFFER | D3D12_RESOURCE_STATE_RENDER_TARGET | D3D12_RESOURCE_STATE_DEPTH_WRITE | D3D12_RESOURCE_STATE_DEPTH_READ |
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_STREAM_OUT | D3D12_RESOURCE_STATE_RESOLVE_DEST |
    D3D12_RESOURCE_STATE_RESOLVE_SOURCE;

inline u32 lowestBit(u64 m) { return (u32)__builtin_ctzll(m); }
inline u32 highestBit(u64 m) { return 63u - (u32)__builtin_clzll(m); }
inline u64 rangeMask(u32 range) { return range == 0 ? 0xffffffffull : (0xffffull << kLocalSRVSlots); }

bool viewsOverlap(const ViewObj* a, const ViewObj* b) {
    if (a->res != b->res) return false;
    if (a->res->kind == RES_BUFFER) return true;
    return a->mip0 < b->mip0 + b->mipCount && b->mip0 < a->mip0 + a->mipCount && a->slice0 < b->slice0 + b->sliceCount &&
           b->slice0 < a->slice0 + a->sliceCount;
}

D3D12_PRIMITIVE_TOPOLOGY d3dTopology(Topology t) {
    switch (t) {
        case TOPO_TRIANGLE_STRIP: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        case TOPO_LINE_LIST: return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
        case TOPO_LINE_STRIP: return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
        case TOPO_POINT_LIST: return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
        default: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    }
}
D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType(Topology t) {
    switch (t) {
        case TOPO_LINE_LIST:
        case TOPO_LINE_STRIP: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        case TOPO_POINT_LIST: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
        default: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    }
}

const char* stateName(D3D12_RESOURCE_STATES s) {
    static char buf[160];
    buf[0] = 0;
    struct N { D3D12_RESOURCE_STATES s; const char* n; };
    static const N names[] = {{D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, "VB_CB"}, {D3D12_RESOURCE_STATE_INDEX_BUFFER, "IB"},
                              {D3D12_RESOURCE_STATE_RENDER_TARGET, "RT"}, {D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "UAV"},
                              {D3D12_RESOURCE_STATE_DEPTH_WRITE, "DEPTH_WRITE"}, {D3D12_RESOURCE_STATE_DEPTH_READ, "DEPTH_READ"},
                              {D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, "NON_PIXEL_SRV"}, {D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, "PIXEL_SRV"},
                              {D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, "INDIRECT"}, {D3D12_RESOURCE_STATE_COPY_DEST, "COPY_DEST"},
                              {D3D12_RESOURCE_STATE_COPY_SOURCE, "COPY_SOURCE"}};
    for (const N& n : names)
        if (s & n.s) {
            if (buf[0]) strcat(buf, "|");
            strcat(buf, n.n);
        }
    if (!buf[0]) strcpy(buf, "COMMON");
    return buf;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// Lifetime
void Context::init(QueueKind q) {
    queueKind = q;
    allocator = acquireAllocator(q);
    checkHR(g.dev->CreateCommandList(0, q == QUEUE_DIRECT ? D3D12_COMMAND_LIST_TYPE_DIRECT : D3D12_COMMAND_LIST_TYPE_COMPUTE, allocator,
                                     nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&cl), "CreateCommandList");
    recording = true;
    listId = ++g.listCounter;
    anyWork = false;
    invalidateApplied();
    ID3D12DescriptorHeap* heaps[] = {g.gpuHeap, g.samplerHeap};
    cl->SetDescriptorHeaps(2, heaps);
}

void Context::beginList() {
    allocator = acquireAllocator(queueKind);
    checkHR(cl->Reset(allocator, nullptr), "ID3D12GraphicsCommandList::Reset");
    recording = true;
    listId = ++g.listCounter;
    anyWork = false;
    invalidateApplied();
    ID3D12DescriptorHeap* heaps[] = {g.gpuHeap, g.samplerHeap};
    cl->SetDescriptorHeaps(2, heaps);
}

void Context::invalidateApplied() {
    curPSO = nullptr;
    curGraphicsRS = nullptr;
    curComputeRS = nullptr;
    memset(appliedCB, 0, sizeof(appliedCB));
    memset(appliedVB, 0, sizeof(appliedVB));
    appliedVBCount = 0;
    memset(&appliedIB, 0, sizeof(appliedIB));
    for (auto& s : srvTableDirty) s[0] = s[1] = true;
    uavTableDirty = true;
    rtDirty = vpDirty = scissorDirty = topoDirty = true;
    rootConstDirty[0] = rootConstDirty[1] = true;
    bindlessApplied[0] = bindlessApplied[1] = false;
    statesDirty[0] = statesDirty[1] = true;
    iaStatesDirty = true;
}

u64 Context::submit() {
    if (!recording) FatalError("Direct3D 12: submit on a context that is not recording");
    flushBarriers();
    while (eventDepth > 0) {
        cl->EndEvent();
        eventDepth--;
    }
    checkHR(cl->Close(), "ID3D12GraphicsCommandList::Close");
    ID3D12CommandList* lists[] = {cl};
    g.queue[queueKind]->ExecuteCommandLists(1, lists);
    u64 v = ++g.fenceValue[queueKind];
    checkHR(g.queue[queueKind]->Signal(g.fence[queueKind], v), "ID3D12CommandQueue::Signal");
    g.allocators[queueKind].push_back({allocator, v});
    allocator = nullptr;
    recording = false;
    onSubmitted(queueKind, v);
    if (queueKind == QUEUE_DIRECT) beginList();
    return v;
}

void Context::wait(QueueKind q, u64 value) {
    if (q == queueKind || value == 0) return;
    if (queueKind == QUEUE_DIRECT && anyWork) submit();
    checkHR(g.queue[queueKind]->Wait(g.fence[q], value), "ID3D12CommandQueue::Wait");
}

void Context::markSRVTablesDirty() {
    for (auto& s : srvTableDirty) s[0] = s[1] = true;
}

void Context::onRelease(ResourceObj* r) {
    for (int st = 0; st < STAGE_COUNT; st++)
        for (u32 i = 0; i < kMaxCBSlots; i++)
            if (cbs[st][i] == r) cbs[st][i] = nullptr;
    for (u32 i = 0; i < kMaxVertexBuffers; i++)
        if (vbs[i] == r) vbs[i] = nullptr;
    if (ib == r) ib = nullptr;
}

void Context::onViewRelease(ViewObj* v) {
    for (int st = 0; st < STAGE_COUNT; st++)
        for (u32 i = 0; i < kMaxSRVSlots; i++)
            if (srvs[st][i] == v) {
                srvs[st][i] = nullptr;
                srvTableDirty[st][i >= kLocalSRVSlots ? 1 : 0] = true;
            }
    for (u32 i = 0; i < kMaxUAVSlots; i++)
        if (uavs[i] == v) {
            uavs[i] = nullptr;
            uavTableDirty = true;
        }
    for (u32 i = 0; i < numRTV; i++)
        if (rtvs[i] == v) {
            rtvs[i] = nullptr;
            rtDirty = psoDirty = true;
        }
    if (dsvBound == v) {
        dsvBound = nullptr;
        rtDirty = psoDirty = true;
    }
}

void Context::onShaderRelease(ShaderObj* s) {
    for (int st = 0; st < STAGE_COUNT; st++)
        if (shaders[st] == s) {
            shaders[st] = nullptr;
            psoDirty = true;
        }
    if (s->computePSO && curPSO == s->computePSO) curPSO = nullptr;
    graphicsPSO = nullptr;
    psoDirty = true;
}

void Context::checkGraphics(const char* what) const {
    if (queueKind != QUEUE_DIRECT) FatalError("Direct3D 12: %s is not available on the async compute queue", what);
}

D3D12_RESOURCE_STATES Context::srvState() const {
    return queueKind == QUEUE_DIRECT ? kSrvStateDirect : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
}

// ---------------------------------------------------------------------------------------------------------------
// Bindings
void Context::setVS(ShaderObj* vs) {
    checkGraphics("setVS");
    if (vs && vs->stage != STAGE_VS) FatalError("Direct3D 12: %s is not a vertex shader", vs->name.c_str());
    if (shaders[STAGE_VS] == vs) return;
    shaders[STAGE_VS] = vs;
    psoDirty = true;
    srvTableDirty[STAGE_VS][0] = srvTableDirty[STAGE_VS][1] = true;
    statesDirty[0] = true;
}

void Context::setPS(PixelShader ps) {
    checkGraphics("setPS");
    if (ps && ps->stage != STAGE_PS) FatalError("Direct3D 12: %s is not a pixel shader", ps->name.c_str());
    if (shaders[STAGE_PS] == ps) return;
    shaders[STAGE_PS] = ps;
    psoDirty = true;
    srvTableDirty[STAGE_PS][0] = srvTableDirty[STAGE_PS][1] = true;
    statesDirty[0] = true;
}

void Context::setCS(ComputeShader cs) {
    if (cs && cs->stage != STAGE_CS) FatalError("Direct3D 12: %s is not a compute shader", cs->name.c_str());
    if (shaders[STAGE_CS] == cs) return;
    shaders[STAGE_CS] = cs;
    srvTableDirty[STAGE_CS][0] = srvTableDirty[STAGE_CS][1] = true;
    uavTableDirty = true;
    statesDirty[1] = true;
}

void Context::setInputLayout(InputLayout il) {
    if (inputLayout == il) return;
    inputLayout = il;
    psoDirty = true;
}

void Context::setTopology(Topology t) {
    if (topology == t) return;
    if (topologyType(t) != topologyType(topology)) psoDirty = true;
    topology = t;
    topoDirty = true;
}

void Context::setBlendState(BlendState s) {
    if (blend == s) return;
    blend = s;
    psoDirty = true;
}

void Context::setDepthState(DepthState s) {
    if (depthState == s) return;
    depthState = s;
    psoDirty = true;
}

void Context::setRasterState(RasterState s) {
    if (raster == s) return;
    bool oldScissor = raster && raster->desc.scissor, newScissor = s && s->desc.scissor;
    if (oldScissor != newScissor) scissorDirty = true;
    raster = s;
    psoDirty = true;
}

void Context::setSRVs(Stage st, u32 slot, u32 n, const SRV* v) {
    if (slot + n > kMaxSRVSlots) FatalError("Direct3D 12: SRV slots t%u..t%u are outside t0..t%u", slot, slot + n - 1, kMaxSRVSlots - 1);
    for (u32 i = 0; i < n; i++) {
        u32 s = slot + i;
        SRV nv = v ? v[i] : nullptr;
        checkAlive(nv, "an SRV binding");
        if (srvs[st][s] == nv) continue;
        srvs[st][s] = nv;
        srvTableDirty[st][s >= kLocalSRVSlots ? 1 : 0] = true;
        statesDirty[st == STAGE_CS ? 1 : 0] = true;
    }
}

void Context::setCBs(Stage st, u32 slot, u32 n, const Resource* b) {
    if (slot + n > kMaxCBSlots) FatalError("Direct3D 12: constant buffer slots b%u..b%u are outside b0..b3", slot, slot + n - 1);
    for (u32 i = 0; i < n; i++) {
        Resource r = b ? b[i] : nullptr;
        checkAlive(r, "a constant buffer binding");
        if (cbs[st][slot + i] == r) continue;
        cbs[st][slot + i] = r;
        if (r && !r->upload) statesDirty[st == STAGE_CS ? 1 : 0] = true;
    }
}

void Context::csSetUAVs(u32 slot, u32 n, const UAV* v, const u32* initialCounts) {
    if (slot + n > kMaxUAVSlots) FatalError("Direct3D 12: UAV slots u%u..u%u are outside u0..u7", slot, slot + n - 1);
    for (u32 i = 0; i < n; i++) {
        u32 s = slot + i;
        UAV nv = v ? v[i] : nullptr;
        checkAlive(nv, "a UAV binding");
        if (initialCounts && initialCounts[i] != ~0u && nv) {
            uavInitCount[s] = initialCounts[i];
            uavInitMask |= (u8)(1u << s);
        }
        if (uavs[s] == nv) continue;
        uavs[s] = nv;
        uavTableDirty = true;
        srvTableDirty[STAGE_CS][0] = srvTableDirty[STAGE_CS][1] = true;
        statesDirty[1] = true;
    }
}

void Context::setRootConstants(bool compute, u32 offset, u32 count, const void* data) {
    if (offset + count > kRootConstants) FatalError("Direct3D 12: root constants %u..%u out of range", offset, offset + count);
    memcpy(&rootConst[compute ? 1 : 0][offset], data, count * 4);
    rootConstDirty[compute ? 1 : 0] = true;
}

void Context::setVertexBuffers(u32 slot, u32 n, const Resource* b, const u32* strides, const u32* offsets) {
    checkGraphics("setVertexBuffers");
    if (slot + n > kMaxVertexBuffers) FatalError("Direct3D 12: vertex buffer slots %u..%u out of range", slot, slot + n - 1);
    for (u32 i = 0; i < n; i++) {
        checkAlive(b ? b[i] : nullptr, "a vertex buffer binding");
        vbs[slot + i] = b ? b[i] : nullptr;
        vbStride[slot + i] = strides ? strides[i] : 0;
        vbOffset[slot + i] = offsets ? offsets[i] : 0;
    }
    iaStatesDirty = true;
}

void Context::setIndexBuffer(Resource b, DXGI_FORMAT fmt, u32 offset) {
    checkGraphics("setIndexBuffer");
    checkAlive(b, "the index buffer binding");
    ib = b;
    ibFormat = fmt;
    ibOffset = offset;
    iaStatesDirty = true;
}

void Context::setRenderTargets(u32 n, const RTV* rtv, DSV dsv) {
    checkGraphics("setRenderTargets");
    if (n > kMaxRenderTargets) FatalError("Direct3D 12: %u render targets", n);
    for (u32 i = 0; i < n; i++) checkAlive(rtv[i], "a render target binding");
    checkAlive(dsv, "the depth target binding");
    bool same = n == numRTV && dsv == dsvBound;
    for (u32 i = 0; same && i < n; i++) same = rtvs[i] == rtv[i];
    if (same) return;
    for (u32 i = 0; i < kMaxRenderTargets; i++) rtvs[i] = i < n ? rtv[i] : nullptr;
    numRTV = n;
    dsvBound = dsv;
    rtDirty = psoDirty = true;
    srvTableDirty[STAGE_VS][0] = srvTableDirty[STAGE_VS][1] = srvTableDirty[STAGE_PS][0] = srvTableDirty[STAGE_PS][1] = true;
    statesDirty[0] = true;
}

void Context::getRenderTargets(RTV* rtv0, DSV* dsv) const {
    if (rtv0) *rtv0 = numRTV ? rtvs[0] : nullptr;
    if (dsv) *dsv = dsvBound;
}

void Context::setViewport(const Viewport& vp) {
    if (memcmp(&vp, &viewport, sizeof(Viewport)) == 0) return;
    viewport = vp;
    vpDirty = true;
}

void Context::setScissor(int x, int y, int w, int h) {
    scissor.left = x;
    scissor.top = y;
    scissor.right = x + w;
    scissor.bottom = y + h;
    scissorDirty = true;
}

// ---------------------------------------------------------------------------------------------------------------
// Resource state tracking
void Context::require(ResourceObj* r, D3D12_RESOURCE_STATES s) {
    if (!r || r->upload || r->readback) return;
    reqs.push_back({r, 0, r->mips, 0, r->layers, s});
}

void Context::requireView(ViewObj* v, D3D12_RESOURCE_STATES s) {
    ResourceObj* r = v->res;
    if (r->upload || r->readback) return;
    if (v->whole || r->kind == RES_BUFFER) reqs.push_back({r, 0, r->mips, 0, r->layers, s});
    else reqs.push_back({r, v->mip0, v->mipCount, v->slice0, v->sliceCount, s});
}

void Context::pushTransition(ResourceObj* r, u32 sub, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    if (queueKind == QUEUE_COMPUTE && ((before | after) & kGraphicsOnlyStates))
        FatalError("Direct3D 12: async compute cannot transition '%s' from %s to %s: prepare it on the direct context first",
                   r->name.c_str(), stateName(before), stateName(after));
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r->d3d;
    b.Transition.Subresource = sub;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter = after;
    barriers.push_back(b);
    g.stateEpoch++;
}

static void pushUavBarrier(std::vector<D3D12_RESOURCE_BARRIER>& barriers, ResourceObj* r) {
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    b.UAV.pResource = r ? r->d3d : nullptr;
    barriers.push_back(b);
    if (r) r->uavPending = false;
}

void Context::transitionWhole(ResourceObj* r, D3D12_RESOURCE_STATES target) {
    if (!r || r->upload || r->readback || !r->d3d) return;
    bool compute = queueKind == QUEUE_COMPUTE;
    if (r->kind == RES_BUFFER) {
        if (r->stateList != listId) {
            // buffers decay to COMMON when the command list that last used them completes
            r->stateList = listId;
            r->state = D3D12_RESOURCE_STATE_COMMON;
            r->promotedRead = false;
            r->uavPending = false;
        }
        if (r->state == D3D12_RESOURCE_STATE_COMMON) {
            // implicit promotion: buffers go from COMMON to any state without a barrier
            if (target != D3D12_RESOURCE_STATE_COMMON) {
                r->state = target;
                r->promotedRead = isReadState(target);
                g.stateEpoch++;
            }
            return;
        }
    }
    if (r->perSub) {
        for (u32 s = 0; s < r->numSub; s++) transitionSub(r, s, target);
        bool uniform = true;
        for (u32 s = 1; s < r->numSub && uniform; s++) uniform = r->subState[s] == r->subState[0];
        if (uniform) {
            r->perSub = false;
            r->state = r->subState[0];
        }
        return;
    }
    D3D12_RESOURCE_STATES cur = r->state;
    if (cur == target) {
        if ((target & D3D12_RESOURCE_STATE_UNORDERED_ACCESS) && r->uavPending) pushUavBarrier(barriers, r);
        return;
    }
    if (isReadState(target) && isReadState(cur)) {
        if ((cur & target) == target) return;   // already readable that way
        if (r->kind == RES_BUFFER && r->promotedRead) {
            r->state = cur | target;             // implicit promotion into more read states
            g.stateEpoch++;
            return;
        }
        if (!compute) target = cur | target;     // keep the read states it has (no flip-flopping between readers)
    }
    pushTransition(r, kAllSubresources, cur, target);
    r->state = target;
    r->promotedRead = false;
}

void Context::transitionSub(ResourceObj* r, u32 sub, D3D12_RESOURCE_STATES target) {
    if (!r || r->upload || r->readback || !r->d3d) return;
    if (r->kind == RES_BUFFER || r->numSub == 1) {
        transitionWhole(r, target);
        return;
    }
    if (!r->perSub) {
        if (r->state == target) {
            if ((target & D3D12_RESOURCE_STATE_UNORDERED_ACCESS) && r->uavPending) pushUavBarrier(barriers, r);
            return;
        }
        if (isReadState(target) && isReadState(r->state) && (r->state & target) == target) return;
        r->subState.assign(r->numSub, r->state);
        r->perSub = true;
    }
    D3D12_RESOURCE_STATES cur = r->subState[sub];
    if (cur == target) {
        if ((target & D3D12_RESOURCE_STATE_UNORDERED_ACCESS) && r->uavPending) pushUavBarrier(barriers, r);
        return;
    }
    if (isReadState(target) && isReadState(cur)) {
        if ((cur & target) == target) return;
        if (queueKind == QUEUE_DIRECT) target = cur | target;
    }
    pushTransition(r, sub, cur, target);
    r->subState[sub] = target;
}

static D3D12_RESOURCE_STATES combineStates(D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b, ResourceObj* r) {
    if (!a) return b;
    if (a == b) return a;
    if (isWriteState(a) || isWriteState(b)) {
        static int logged = 0;
        if (logged++ < 8)
            LOG("Direct3D 12: '%s' is needed as %s and %s at once; the write wins", r->name.c_str(), stateName(a), stateName(b));
        return isWriteState(a) ? a : b;
    }
    return a | b;
}

void Context::resolveRequirements() {
    if (reqs.empty()) {
        flushBarriers();
        return;
    }
    if (reqs.size() > 1) std::sort(reqs.begin(), reqs.end(), [](const Req& a, const Req& b) { return a.r < b.r; });
    for (size_t i = 0; i < reqs.size();) {
        size_t j = i + 1;
        while (j < reqs.size() && reqs[j].r == reqs[i].r) j++;
        ResourceObj* r = reqs[i].r;
        bool whole = true;
        for (size_t k = i; k < j && whole; k++)
            whole = reqs[k].mip0 == 0 && reqs[k].mipCount >= r->mips && reqs[k].slice0 == 0 && reqs[k].sliceCount >= r->layers;
        if (r->kind == RES_BUFFER || whole || r->numSub == 1) {
            D3D12_RESOURCE_STATES t = (D3D12_RESOURCE_STATES)0;
            for (size_t k = i; k < j; k++) t = combineStates(t, reqs[k].s, r);
            transitionWhole(r, t);
        } else {
            tmpStates.assign(r->numSub, (D3D12_RESOURCE_STATES)0);
            for (size_t k = i; k < j; k++) {
                const Req& q = reqs[k];
                for (u32 sl = q.slice0; sl < q.slice0 + q.sliceCount && sl < r->layers; sl++)
                    for (u32 m = q.mip0; m < q.mip0 + q.mipCount && m < r->mips; m++) {
                        u32 sub = m + sl * r->mips;
                        tmpStates[sub] = combineStates(tmpStates[sub], q.s, r);
                    }
            }
            for (u32 s = 0; s < r->numSub; s++)
                if (tmpStates[s]) transitionSub(r, s, tmpStates[s]);
            if (r->perSub) {
                bool uniform = true;
                for (u32 s = 1; s < r->numSub && uniform; s++) uniform = r->subState[s] == r->subState[0];
                if (uniform) {
                    r->perSub = false;
                    r->state = r->subState[0];
                }
            }
        }
        i = j;
    }
    reqs.clear();
    flushBarriers();
}

void Context::flushBarriers() {
    if (barriers.empty()) return;
    cl->ResourceBarrier((UINT)barriers.size(), barriers.data());
    barriers.clear();
    anyWork = true;
}

void Context::transition(Resource r, D3D12_RESOURCE_STATES state, u32 subresource) {
    if (!r) return;
    if (subresource == kAllSubresources) transitionWhole(r, state);
    else transitionSub(r, subresource, state);
}

void Context::uavBarrier(Resource r) {
    pushUavBarrier(barriers, r);
}

void Context::aliasingBarrier(Resource before, Resource after) {
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
    b.Aliasing.pResourceBefore = before ? before->d3d : nullptr;
    b.Aliasing.pResourceAfter = after ? after->d3d : nullptr;
    barriers.push_back(b);
}

void Context::discard(Resource r) {
    if (!r || !r->d3d) return;
    if (r->desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) transitionWhole(r, D3D12_RESOURCE_STATE_RENDER_TARGET);
    else if (r->desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) transitionWhole(r, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    else if (r->desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) transitionWhole(r, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    flushBarriers();
    cl->DiscardResource(r->d3d, nullptr);
    anyWork = true;
}

bool Context::srvBlockedByOutput(ViewObj* v, bool compute) const {
    if (compute) {
        for (u32 i = 0; i < kMaxUAVSlots; i++)
            if (uavs[i] && viewsOverlap(uavs[i], v)) return true;
        return false;
    }
    for (u32 i = 0; i < numRTV; i++)
        if (rtvs[i] && viewsOverlap(rtvs[i], v)) return true;
    if (dsvBound && !dsvBound->readOnly && viewsOverlap(dsvBound, v)) return true;
    return false;
}

void Context::markUAVWrites() {
    ShaderObj* cs = shaders[STAGE_CS];
    u32 m = cs ? cs->bind.uavMask : 0;
    while (m) {
        u32 s = lowestBit(m);
        m &= m - 1;
        if (UAV v = uavs[s]) {
            v->res->uavPending = true;
            if (v->res->counter) v->res->counter->uavPending = true;
        }
    }
    statesDirty[1] = true;   // the next dispatch orders these writes with UAV barriers
}

// ---------------------------------------------------------------------------------------------------------------
// Draw / dispatch preparation
ID3D12PipelineState* Context::resolveGraphicsPSO() {
    ShaderObj* vs = shaders[STAGE_VS];
    ShaderObj* ps = shaders[STAGE_PS];
    BlendState bs = blend ? blend : g.defaultBlend;
    RasterState rs = raster ? raster : g.defaultRaster;
    DepthState ds = depthState ? depthState : g.defaultDepth;
    PsoKey k;
    memset(&k, 0, sizeof(k));
    k.vs = vs->id;
    k.ps = ps ? ps->id : 0;
    k.il = inputLayout ? inputLayout->id : 0;
    k.blend = bs->id;
    k.raster = rs->id;
    k.depth = ds->id;
    k.topoType = (u32)topologyType(topology);
    k.numRT = numRTV;
    for (u32 i = 0; i < numRTV; i++) k.rt[i] = rtvs[i] ? rtvs[i]->format : DXGI_FORMAT_UNKNOWN;
    k.dsv = dsvBound ? dsvBound->format : DXGI_FORMAT_UNKNOWN;
    k.flags = dsvBound && dsvBound->readOnly ? 1u : 0u;
    auto it = g.psoCache.find(k);
    if (it != g.psoCache.end()) return it->second;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d = {};
    d.pRootSignature = g.rsGraphics;
    d.VS = {vs->code.data(), vs->code.size()};
    if (ps) d.PS = {ps->code.data(), ps->code.size()};
    d.BlendState = bs->d3d;
    d.SampleMask = UINT_MAX;
    d.RasterizerState = rs->d3d;
    d.DepthStencilState = ds->d3d;
    if (!dsvBound) {
        d.DepthStencilState.DepthEnable = FALSE;   // no depth buffer: no depth test (as without a bound DSV)
        d.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    } else if (dsvBound->readOnly)
        d.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;   // read-only depth ignores writes
    if (inputLayout) d.InputLayout = {inputLayout->elems.data(), (UINT)inputLayout->elems.size()};
    d.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
    d.PrimitiveTopologyType = topologyType(topology);
    d.NumRenderTargets = numRTV;
    for (u32 i = 0; i < numRTV; i++) d.RTVFormats[i] = k.rt[i];
    d.DSVFormat = k.dsv;
    d.SampleDesc.Count = 1;
    ID3D12PipelineState* pso = nullptr;
    double t0 = Platform::timeSeconds();
    HRESULT hr = g.dev->CreateGraphicsPipelineState(&d, __uuidof(ID3D12PipelineState), (void**)&pso);
    double took = Platform::timeSeconds() - t0;
    g.psoSeconds += took;
    if (FAILED(hr)) {
        if (hr == DXGI_ERROR_DEVICE_REMOVED) deviceLost("pipeline creation", hr);
        FatalError("Direct3D 12: graphics pipeline (%s + %s, %u targets fmt %d, depth fmt %d) failed (%08lx)", vs->name.c_str(),
                   ps ? ps->name.c_str() : "no pixel shader", numRTV, numRTV ? (int)k.rt[0] : 0, (int)k.dsv, (unsigned long)hr);
    }
    g.psoCount++;
    g.psoCache[k] = pso;
    if (took > 0.25 || Platform::hasArg("psolog"))
        LOG("Pipeline %d: %s + %s (%u targets) in %.0f ms", g.psoCount, vs->name.c_str(), ps ? ps->name.c_str() : "-", numRTV, took * 1000.0);
    return pso;
}

void Context::buildSRVTable(Stage st, u32 range, u32 rootParam, bool compute) {
    ShaderObj* s = shaders[st];
    u64 mask = s->bind.srvMask & rangeMask(range);
    srvTableDirty[st][range] = false;
    if (!mask) return;
    u32 base = range ? kLocalSRVSlots : 0;
    u32 declared = range ? kMaxSRVSlots - kLocalSRVSlots : kLocalSRVSlots;
    u32 count = highestBit(mask) - base + 1;
    D3D12_CPU_DESCRIPTOR_HANDLE src[kMaxSRVSlots];
    UINT ones[kMaxSRVSlots];
    for (u32 i = 0; i < count; i++) {
        u32 slot = base + i;
        ones[i] = 1;
        if (mask & (1ull << slot)) {
            ViewObj* v = srvs[st][slot];
            if (v && v->kind == VIEW_SRV && !srvBlockedByOutput(v, compute)) src[i] = v->cpu;
            else src[i] = nullSRV(s->bind.srvType[slot], s->bind.srvDim[slot], s->bind.srvStride[slot]);
        } else src[i] = nullSRV(D3D_SIT_TEXTURE, D3D_SRV_DIMENSION_TEXTURE2D, 0);
    }
    u32 idx = allocRing(declared);
    D3D12_CPU_DESCRIPTOR_HANDLE dst = gpuHeapCpu(idx);
    UINT dstSize = count;
    g.dev->CopyDescriptors(1, &dst, &dstSize, count, src, ones, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    if (compute) cl->SetComputeRootDescriptorTable(rootParam, gpuHeapGpu(idx));
    else cl->SetGraphicsRootDescriptorTable(rootParam, gpuHeapGpu(idx));
}

void Context::buildUAVTable() {
    ShaderObj* cs = shaders[STAGE_CS];
    uavTableDirty = false;
    D3D12_CPU_DESCRIPTOR_HANDLE src[kMaxUAVSlots];
    UINT ones[kMaxUAVSlots];
    for (u32 i = 0; i < kMaxUAVSlots; i++) {
        ones[i] = 1;
        if (cs->bind.uavMask & (1u << i)) {
            ViewObj* v = uavs[i];
            src[i] = (v && v->kind == VIEW_UAV) ? v->cpu : nullUAV(cs->bind.uavType[i], cs->bind.uavDim[i], cs->bind.uavStride[i]);
        } else src[i] = nullUAV(D3D_SIT_UAV_RWTYPED, D3D_SRV_DIMENSION_BUFFER, 0);
    }
    u32 idx = allocRing(kMaxUAVSlots);
    D3D12_CPU_DESCRIPTOR_HANDLE dst = gpuHeapCpu(idx);
    UINT dstSize = kMaxUAVSlots;
    g.dev->CopyDescriptors(1, &dst, &dstSize, kMaxUAVSlots, src, ones, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    cl->SetComputeRootDescriptorTable(CRP_UAV, gpuHeapGpu(idx));
}

void Context::applyRootCBVs(Stage st, bool compute) {
    ShaderObj* s = shaders[st];
    u32 m = s->bind.cbMask;
    while (m) {
        u32 slot = lowestBit(m);
        m &= m - 1;
        ResourceObj* r = cbs[st][slot];
        D3D12_GPU_VIRTUAL_ADDRESS va = (r && r->gpuVA) ? r->gpuVA : g.zeroCB.buf->gpuVA;
        if (appliedCB[st][slot] == va) continue;
        appliedCB[st][slot] = va;
        if (compute) cl->SetComputeRootConstantBufferView(CRP_CB + slot, va);
        else cl->SetGraphicsRootConstantBufferView((st == STAGE_VS ? GRP_CB_VS : GRP_CB_PS) + slot, va);
    }
}

void Context::applyPendingCounters() {
    u32 m = uavInitMask;
    uavInitMask = 0;
    while (m) {
        u32 s = lowestBit(m);
        m &= m - 1;
        UAV v = uavs[s];
        if (!v || !v->res->counter) continue;
        ResourceObj* c = v->res->counter;
        UploadAlloc a = allocDynamic(4, 4);
        memcpy(a.cpu, &uavInitCount[s], 4);
        transitionWhole(c, D3D12_RESOURCE_STATE_COPY_DEST);
        flushBarriers();
        cl->CopyBufferRegion(c->d3d, 0, a.res, a.offset, 4);
    }
    statesDirty[1] = true;
}

void Context::prepareDraw() {
    checkGraphics("draw");
    ShaderObj* vs = shaders[STAGE_VS];
    ShaderObj* ps = shaders[STAGE_PS];
    if (!vs) FatalError("Direct3D 12: draw without a vertex shader");
    if (curGraphicsRS != g.rsGraphics) {
        cl->SetGraphicsRootSignature(g.rsGraphics);
        cl->SetGraphicsRootDescriptorTable(GRP_SAMPLERS, g.samplerHeapGpu);
        curGraphicsRS = g.rsGraphics;
        memset(appliedCB[STAGE_VS], 0, sizeof(appliedCB[STAGE_VS]));
        memset(appliedCB[STAGE_PS], 0, sizeof(appliedCB[STAGE_PS]));
        srvTableDirty[STAGE_VS][0] = srvTableDirty[STAGE_VS][1] = srvTableDirty[STAGE_PS][0] = srvTableDirty[STAGE_PS][1] = true;
        rootConstDirty[0] = true;
        bindlessApplied[0] = false;
    }
    if (psoDirty || !graphicsPSO) {
        graphicsPSO = resolveGraphicsPSO();
        psoDirty = false;
    }
    if (curPSO != graphicsPSO) {
        cl->SetPipelineState(graphicsPSO);
        curPSO = graphicsPSO;
    }
    // resource states
    bool full = statesDirty[0] || appliedEpoch[0] != g.stateEpoch;
    if (full) {
        for (u32 i = 0; i < numRTV; i++)
            if (rtvs[i]) requireView(rtvs[i], D3D12_RESOURCE_STATE_RENDER_TARGET);
        if (dsvBound) requireView(dsvBound, dsvBound->readOnly ? D3D12_RESOURCE_STATE_DEPTH_READ : D3D12_RESOURCE_STATE_DEPTH_WRITE);
        for (int st = STAGE_VS; st <= STAGE_PS; st++) {
            ShaderObj* s = shaders[st];
            if (!s) continue;
            u64 m = s->bind.srvMask;
            while (m) {
                u32 slot = lowestBit(m);
                m &= m - 1;
                ViewObj* v = srvs[st][slot];
                if (v && !v->res->upload && !srvBlockedByOutput(v, false)) requireView(v, kSrvStateDirect);
            }
            u32 cm = s->bind.cbMask;
            while (cm) {
                u32 slot = lowestBit(cm);
                cm &= cm - 1;
                ResourceObj* r = cbs[st][slot];
                if (r && !r->upload) require(r, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
            }
        }
    }
    if (full || iaStatesDirty) {
        for (u32 i = 0; i < kMaxVertexBuffers; i++)
            if (vbs[i] && !vbs[i]->upload) require(vbs[i], D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        if (ib && !ib->upload) require(ib, D3D12_RESOURCE_STATE_INDEX_BUFFER);
    }
    resolveRequirements();
    if (full || iaStatesDirty) {
        statesDirty[0] = false;
        iaStatesDirty = false;
        appliedEpoch[0] = g.stateEpoch;
    }
    // descriptor tables and root parameters
    for (int st = STAGE_VS; st <= STAGE_PS; st++) {
        ShaderObj* s = shaders[st];
        if (!s) continue;
        for (u32 range = 0; range < 2; range++)
            if (srvTableDirty[st][range]) {
                u32 param = st == STAGE_VS ? (range ? GRP_SRV_VS_GLOBAL : GRP_SRV_VS_LOCAL) : (range ? GRP_SRV_PS_GLOBAL : GRP_SRV_PS_LOCAL);
                buildSRVTable((Stage)st, range, param, false);
            }
        applyRootCBVs((Stage)st, false);
    }
    if (rootConstDirty[0] && (vs->bind.rootConstants || (ps && ps->bind.rootConstants))) {
        cl->SetGraphicsRoot32BitConstants(GRP_ROOT_CONSTANTS, kRootConstants, rootConst[0], 0);
        rootConstDirty[0] = false;
    }
    if (!bindlessApplied[0] && (vs->bind.bindless || (ps && ps->bind.bindless))) {
        cl->SetGraphicsRootDescriptorTable(GRP_BINDLESS, g.gpuHeapGpu);
        bindlessApplied[0] = true;
    }
    // input assembler
    if (topoDirty) {
        cl->IASetPrimitiveTopology(d3dTopology(topology));
        topoDirty = false;
    }
    D3D12_VERTEX_BUFFER_VIEW vbv[kMaxVertexBuffers] = {};
    u32 nvb = 0;
    for (u32 i = 0; i < kMaxVertexBuffers; i++) {
        ResourceObj* r = vbs[i];
        if (!r) continue;
        nvb = i + 1;
        u32 size = r->upload ? r->uploadSize : r->size;
        if (r->gpuVA && size > vbOffset[i]) {
            vbv[i].BufferLocation = r->gpuVA + vbOffset[i];
            vbv[i].SizeInBytes = size - vbOffset[i];
            vbv[i].StrideInBytes = vbStride[i];
        }
    }
    if (nvb && (nvb != appliedVBCount || memcmp(vbv, appliedVB, nvb * sizeof(D3D12_VERTEX_BUFFER_VIEW)) != 0)) {
        cl->IASetVertexBuffers(0, nvb, vbv);
        memcpy(appliedVB, vbv, sizeof(vbv));
        appliedVBCount = nvb;
    }
    if (ib) {
        D3D12_INDEX_BUFFER_VIEW ibv = {};
        u32 size = ib->upload ? ib->uploadSize : ib->size;
        if (ib->gpuVA && size > ibOffset) {
            ibv.BufferLocation = ib->gpuVA + ibOffset;
            ibv.SizeInBytes = size - ibOffset;
            ibv.Format = ibFormat;
        }
        if (memcmp(&ibv, &appliedIB, sizeof(ibv)) != 0) {
            cl->IASetIndexBuffer(&ibv);
            appliedIB = ibv;
        }
    }
    // output merger and rasterizer
    if (rtDirty) {
        D3D12_CPU_DESCRIPTOR_HANDLE h[kMaxRenderTargets];
        for (u32 i = 0; i < numRTV; i++) h[i] = rtvs[i] ? rtvs[i]->cpu : nullRTV();
        D3D12_CPU_DESCRIPTOR_HANDLE dh = dsvBound ? dsvBound->cpu : D3D12_CPU_DESCRIPTOR_HANDLE{0};
        cl->OMSetRenderTargets(numRTV, numRTV ? h : nullptr, FALSE, dsvBound ? &dh : nullptr);
        rtDirty = false;
    }
    if (vpDirty) {
        D3D12_VIEWPORT v = {viewport.x, viewport.y, viewport.w, viewport.h, viewport.minZ, viewport.maxZ};
        cl->RSSetViewports(1, &v);
        vpDirty = false;
    }
    if (scissorDirty) {
        D3D12_RECT r = (raster && raster->desc.scissor) ? scissor : D3D12_RECT{0, 0, 16384, 16384};
        cl->RSSetScissorRects(1, &r);
        scissorDirty = false;
    }
    anyWork = true;
}

void Context::prepareDispatch() {
    ShaderObj* cs = shaders[STAGE_CS];
    if (!cs) FatalError("Direct3D 12: dispatch without a compute shader");
    if (curComputeRS != g.rsCompute) {
        cl->SetComputeRootSignature(g.rsCompute);
        cl->SetComputeRootDescriptorTable(CRP_SAMPLERS, g.samplerHeapGpu);
        curComputeRS = g.rsCompute;
        memset(appliedCB[STAGE_CS], 0, sizeof(appliedCB[STAGE_CS]));
        srvTableDirty[STAGE_CS][0] = srvTableDirty[STAGE_CS][1] = true;
        uavTableDirty = true;
        rootConstDirty[1] = true;
        bindlessApplied[1] = false;
    }
    if (curPSO != cs->computePSO) {
        cl->SetPipelineState(cs->computePSO);
        curPSO = cs->computePSO;
    }
    if (uavInitMask) applyPendingCounters();
    bool full = statesDirty[1] || appliedEpoch[1] != g.stateEpoch;
    if (full) {
        u32 um = cs->bind.uavMask;
        while (um) {
            u32 slot = lowestBit(um);
            um &= um - 1;
            if (UAV v = uavs[slot]) {
                requireView(v, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                if (v->res->counter) require(v->res->counter, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }
        }
        D3D12_RESOURCE_STATES srvSt = srvState();
        u64 m = cs->bind.srvMask;
        while (m) {
            u32 slot = lowestBit(m);
            m &= m - 1;
            ViewObj* v = srvs[STAGE_CS][slot];
            if (v && !v->res->upload && !srvBlockedByOutput(v, true)) requireView(v, srvSt);
        }
        u32 cm = cs->bind.cbMask;
        while (cm) {
            u32 slot = lowestBit(cm);
            cm &= cm - 1;
            ResourceObj* r = cbs[STAGE_CS][slot];
            if (r && !r->upload) require(r, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        }
    }
    resolveRequirements();
    if (full) {
        statesDirty[1] = false;
        appliedEpoch[1] = g.stateEpoch;
    }
    for (u32 range = 0; range < 2; range++)
        if (srvTableDirty[STAGE_CS][range]) buildSRVTable(STAGE_CS, range, range ? CRP_SRV_GLOBAL : CRP_SRV_LOCAL, true);
    if (uavTableDirty && cs->bind.uavMask) buildUAVTable();
    applyRootCBVs(STAGE_CS, true);
    if (rootConstDirty[1] && cs->bind.rootConstants) {
        cl->SetComputeRoot32BitConstants(CRP_ROOT_CONSTANTS, kRootConstants, rootConst[1], 0);
        rootConstDirty[1] = false;
    }
    if (!bindlessApplied[1] && cs->bind.bindless) {
        cl->SetComputeRootDescriptorTable(CRP_BINDLESS, g.gpuHeapGpu);
        bindlessApplied[1] = true;
    }
    anyWork = true;
}

// ---------------------------------------------------------------------------------------------------------------
// Work
void Context::draw(u32 vertexCount, u32 startVertex) {
    if (!vertexCount) return;
    prepareDraw();
    cl->DrawInstanced(vertexCount, 1, startVertex, 0);
}

void Context::drawIndexed(u32 indexCount, u32 startIndex, int baseVertex) {
    if (!indexCount) return;
    prepareDraw();
    cl->DrawIndexedInstanced(indexCount, 1, startIndex, baseVertex, 0);
}

void Context::drawInstanced(u32 vertsPerInstance, u32 instances, u32 startVertex, u32 startInstance) {
    if (!vertsPerInstance || !instances) return;
    prepareDraw();
    cl->DrawInstanced(vertsPerInstance, instances, startVertex, startInstance);
}

void Context::drawIndexedInstanced(u32 indicesPerInstance, u32 instances, u32 startIndex, int baseVertex, u32 startInstance) {
    if (!indicesPerInstance || !instances) return;
    prepareDraw();
    cl->DrawIndexedInstanced(indicesPerInstance, instances, startIndex, baseVertex, startInstance);
}

static void indirectArgs(Resource r, u64 offset, ID3D12Resource*& res, u64& off) {
    if (r->upload) {
        res = r->uploadPage;
        off = r->uploadOffset + offset;
    } else {
        res = r->d3d;
        off = offset;
    }
}

void Context::drawInstancedIndirect(Resource args, u32 offset) {
    if (!args) return;
    checkAlive(args, "drawInstancedIndirect");
    require(args, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    prepareDraw();
    ID3D12Resource* res;
    u64 off;
    indirectArgs(args, offset, res, off);
    if (!res) return;
    cl->ExecuteIndirect(g.drawSig, 1, res, off, nullptr, 0);
}

void Context::dispatch(u32 x, u32 y, u32 z) {
    if (!x || !y || !z) return;
    prepareDispatch();
    cl->Dispatch(x, y, z);
    markUAVWrites();
}

void Context::dispatchIndirect(Resource args, u32 offset) {
    if (!args) return;
    checkAlive(args, "dispatchIndirect");
    require(args, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    prepareDispatch();
    ID3D12Resource* res;
    u64 off;
    indirectArgs(args, offset, res, off);
    if (!res) return;
    cl->ExecuteIndirect(g.dispatchSig, 1, res, off, nullptr, 0);
    markUAVWrites();
}

void Context::executeIndirect(CommandSignature sig, u32 maxCount, Resource args, u64 argOffset, Resource count, u64 countOffset) {
    if (!sig || !args || !maxCount) return;
    checkAlive(args, "executeIndirect");
    checkAlive(count, "executeIndirect");
    require(args, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    if (count) require(count, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    bool compute = sig->kind == INDIRECT_DISPATCH;
    if (compute) prepareDispatch();
    else prepareDraw();
    ID3D12Resource *ares, *cres = nullptr;
    u64 aoff, coff = 0;
    indirectArgs(args, argOffset, ares, aoff);
    if (count) indirectArgs(count, countOffset, cres, coff);
    if (!ares) return;
    cl->ExecuteIndirect(sig->d3d, maxCount, ares, aoff, cres, coff);
    if (sig->rootConstants) rootConstDirty[compute ? 1 : 0] = true;   // the commands changed them
    if (compute) markUAVWrites();
}

// ---------------------------------------------------------------------------------------------------------------
// Clears and copies
void Context::clearRTV(RTV v, const float color[4]) {
    checkGraphics("clearRTV");
    if (!v) return;
    checkAlive(v, "clearRTV");
    requireView(v, D3D12_RESOURCE_STATE_RENDER_TARGET);
    resolveRequirements();
    cl->ClearRenderTargetView(v->cpu, color, 0, nullptr);
    anyWork = true;
}

void Context::clearDepth(DSV v, float depth) {
    checkGraphics("clearDepth");
    if (!v) return;
    checkAlive(v, "clearDepth");
    requireView(v, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    resolveRequirements();
    cl->ClearDepthStencilView(v->cpu, D3D12_CLEAR_FLAG_DEPTH, depth, 0, 0, nullptr);
    anyWork = true;
}

void Context::clearUAVFloat(UAV v, const float values[4]) {
    if (!v) return;
    checkAlive(v, "clearUAVFloat");
    requireView(v, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    resolveRequirements();
    cl->ClearUnorderedAccessViewFloat(gpuHeapGpu(v->bindless), v->cpu, v->res->d3d, values, 0, nullptr);
    v->res->uavPending = true;
    statesDirty[1] = true;
    anyWork = true;
    afterUAVClear();
}

void Context::clearUAVUint(UAV v, const u32 values[4]) {
    if (!v) return;
    checkAlive(v, "clearUAVUint");
    requireView(v, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    resolveRequirements();
    UINT vals[4] = {values[0], values[1], values[2], values[3]};
    cl->ClearUnorderedAccessViewUint(gpuHeapGpu(v->bindless), v->cpu, v->res->d3d, vals, 0, nullptr);
    v->res->uavPending = true;
    statesDirty[1] = true;
    anyWork = true;
    afterUAVClear();
}

// vkd3d 1.10 clears UAVs with an internal compute pipeline and does not restore all of the list's compute root
// arguments afterwards (found on the test rig: a dispatch after a clear read its global SRV table at the wrong heap
// offset). The compute root signature and every compute root argument are set again before the next dispatch; on
// other drivers that only costs a few redundant calls.
void Context::afterUAVClear() {
    curComputeRS = nullptr;
    curPSO = nullptr;
}

void Context::copyResource(Resource dst, Resource src) {
    if (!dst || !src) return;
    checkAlive(dst, "copyResource");
    checkAlive(src, "copyResource");
    if (src->upload) {
        if (!src->uploadPage) return;
        require(dst, D3D12_RESOURCE_STATE_COPY_DEST);
        resolveRequirements();
        cl->CopyBufferRegion(dst->d3d, 0, src->uploadPage, src->uploadOffset, Min<u64>(dst->desc.Width, src->uploadSize));
        anyWork = true;
        return;
    }
    require(dst, D3D12_RESOURCE_STATE_COPY_DEST);
    require(src, D3D12_RESOURCE_STATE_COPY_SOURCE);
    resolveRequirements();
    cl->CopyResource(dst->d3d, src->d3d);
    anyWork = true;
}

void Context::copySubresource(Resource dst, u32 dstSub, Resource src, u32 srcSub) {
    if (!dst || !src) return;
    checkAlive(dst, "copySubresource");
    checkAlive(src, "copySubresource");
    reqs.push_back({dst, dstSub % dst->mips, 1, dstSub / dst->mips, 1, D3D12_RESOURCE_STATE_COPY_DEST});
    reqs.push_back({src, srcSub % src->mips, 1, srcSub / src->mips, 1, D3D12_RESOURCE_STATE_COPY_SOURCE});
    resolveRequirements();
    D3D12_TEXTURE_COPY_LOCATION d = {}, s = {};
    d.pResource = dst->d3d;
    d.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    d.SubresourceIndex = dstSub;
    s.pResource = src->d3d;
    s.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    s.SubresourceIndex = srcSub;
    cl->CopyTextureRegion(&d, 0, 0, 0, &s, nullptr);
    anyWork = true;
}

void Context::copyBufferRegion(Resource dst, u64 dstOffset, Resource src, u64 srcOffset, u64 size) {
    if (!dst || !src || !size) return;
    checkAlive(dst, "copyBufferRegion");
    checkAlive(src, "copyBufferRegion");
    require(dst, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12Resource* sres = src->d3d;
    u64 soff = srcOffset;
    if (src->upload) {
        sres = src->uploadPage;
        soff = src->uploadOffset + srcOffset;
        if (!sres) return;
    } else require(src, D3D12_RESOURCE_STATE_COPY_SOURCE);
    resolveRequirements();
    cl->CopyBufferRegion(dst->d3d, dstOffset, sres, soff, size);
    anyWork = true;
}

void Context::copyStructureCount(Resource dst, u32 dstOffset, UAV src) {
    if (!dst || !src) return;
    checkAlive(dst, "copyStructureCount");
    checkAlive(src, "copyStructureCount");
    ResourceObj* c = src->res->counter;
    if (!c) FatalError("Direct3D 12: copyStructureCount on '%s' without a counter (BUF_APPEND)", src->res->name.c_str());
    require(c, D3D12_RESOURCE_STATE_COPY_SOURCE);
    require(dst, D3D12_RESOURCE_STATE_COPY_DEST);
    resolveRequirements();
    cl->CopyBufferRegion(dst->d3d, dstOffset, c->d3d, 0, 4);
    anyWork = true;
}

void Context::generateMips(const Texture& t) {
    if (!t.res) return;
    checkAlive(t.res, "generateMips");
    if (t.genSrvs.size() < (size_t)t.mips || t.genUavs.size() < (size_t)t.mips)
        FatalError("Direct3D 12: generateMips on '%s' without TEX_GENMIPS", t.res->name.c_str());
    // the generator borrows CS / t0 / u0 / root constants 0..3: restore them for the caller
    ComputeShader oldCS = shaders[STAGE_CS];
    SRV oldSrv = srvs[STAGE_CS][0];
    UAV oldUav = uavs[0];
    u32 oldRC[4];
    memcpy(oldRC, rootConst[1], sizeof(oldRC));
    setCS(g.mipGenCS);
    for (int m = 1; m < t.mips; m++) {
        u32 w = (u32)Max(1, t.width >> m), h = (u32)Max(1, t.height >> m);
        u32 rc[4] = {w, h, t.srgb ? 1u : 0u, (u32)t.layers};
        setRootConstants(true, 0, 4, rc);
        csSetSRVs(0, 1, &t.genSrvs[(size_t)m - 1]);
        csSetUAVs(0, 1, &t.genUavs[(size_t)m]);
        dispatch(divUp(w, 8), divUp(h, 8), (u32)t.layers);
    }
    csSetUAVs(0, 1, &oldUav);
    csSetSRVs(0, 1, &oldSrv);
    setCS(oldCS);
    setRootConstants(true, 0, 4, oldRC);
}

// ---------------------------------------------------------------------------------------------------------------
void Context::beginEvent(const char* name) {
    if (!cl || !recording) return;
    cl->BeginEvent(1, name, (UINT)strlen(name) + 1);   // metadata 1: ANSI string
    eventDepth++;
}

void Context::endEvent() {
    if (!cl || !recording || eventDepth <= 0) return;
    cl->EndEvent();
    eventDepth--;
}

}  // namespace gfx
