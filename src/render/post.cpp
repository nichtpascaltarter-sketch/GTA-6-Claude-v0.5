// Post processing: exposure adaptation and tonemapping to the back buffer. Included from renderer.cpp.
namespace Render {

struct PostCBData {
    vec4 p0, p1, p2, p3;
};

struct PostSystem {
    gfx::Buffer exposureBuf, lumPartial;
    gfx::Texture whiteTex, blackTex;
    ID3D11ComputeShader *csReduce = nullptr, *csExposure = nullptr;
    ID3D11PixelShader* psTonemap = nullptr;
    gfx::CBuffer<PostCBData> cb;
    int partialCount = 0;
    float exposureCompensation = 0.3f;

    void init() {
        float initExp[4] = {2.5e-5f, 15.f, 0.f, 0.f};
        exposureBuf = gfx::createBuffer(16, 16, gfx::BUF_STRUCTURED | gfx::BUF_UAV, initExp);
        u32 white = 0xffffffffu, black = 0xff000000u;
        whiteTex = gfx::createTexture2D(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, gfx::TEX_SRV, 1, 1, &white, 4);
        blackTex = gfx::createTexture2D(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, gfx::TEX_SRV, 1, 1, &black, 4);
        csReduce = gfx::loadCS("post.hlsl", "csLumReduce");
        csExposure = gfx::loadCS("post.hlsl", "csExposure");
        psTonemap = gfx::loadPS("post.hlsl", "psTonemap");
        cb.create();
    }

    void resize(int w, int h) {
        lumPartial.release();
        int gx = (w + 63) / 64, gy = (h + 63) / 64;
        partialCount = gx * gy;
        lumPartial = gfx::createBuffer((u32)(partialCount * 8), 4, gfx::BUF_STRUCTURED | gfx::BUF_UAV);
    }

    void render(Renderer& r, float dt) {
        auto* c = gfx::ctx;
        cb.data.p0 = vec4(exposureCompensation, 2.2f, 1.4f, Clamp(dt, 0.f, 0.25f));
        cb.data.p1 = vec4(0.04f, 0.22f, 0.012f, 1.08f);
        cb.data.p2 = vec4(1.04f, 0.6f, -3.f, 16.f);
        cb.data.p3 = vec4((float)partialCount, r.cameraCut ? 1.f : 0.f, 0, 0);
        cb.upload();
        ID3D11Buffer* cbs[] = {r.frameCB.get(), cb.get()};
        c->CSSetConstantBuffers(0, 2, cbs);
        c->PSSetConstantBuffers(0, 2, cbs);
        // Exposure: reduce luminance, then adapt (exposure buffer read at t40 during reduce -> use copy via SRV)
        c->CSSetShader(csReduce, nullptr, 0);
        c->CSSetShaderResources(0, 1, &r.hdr.srv);
        c->CSSetShaderResources(40, 1, &exposureBuf.srv);
        c->CSSetUnorderedAccessViews(0, 1, &lumPartial.uav, nullptr);
        c->Dispatch(gfx::divUp(r.width, 64), gfx::divUp(r.height, 64), 1);
        ID3D11ShaderResourceView* nullSrv[3] = {};
        ID3D11UnorderedAccessView* nullUav[2] = {};
        c->CSSetUnorderedAccessViews(0, 1, nullUav, nullptr);
        c->CSSetShaderResources(0, 3, nullSrv);
        c->CSSetShaderResources(40, 1, nullSrv);
        c->CSSetShader(csExposure, nullptr, 0);
        c->CSSetShaderResources(2, 1, &lumPartial.srv);
        c->CSSetUnorderedAccessViews(1, 1, &exposureBuf.uav, nullptr);
        c->Dispatch(1, 1, 1);
        c->CSSetUnorderedAccessViews(0, 2, nullUav, nullptr);
        c->CSSetShaderResources(0, 3, nullSrv);

        // Tonemap to back buffer
        ID3D11RenderTargetView* bb = gfx::backbufferRTV();
        c->OMSetRenderTargets(1, &bb, nullptr);
        gfx::setViewport((float)r.outWidth, (float)r.outHeight);
        c->OMSetDepthStencilState(gfx::states.depthOff, 0);
        c->OMSetBlendState(gfx::states.opaque, nullptr, 0xffffffff);
        c->RSSetState(gfx::states.cullNone);
        c->IASetInputLayout(nullptr);
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        c->VSSetShader(r.vsFullscreen.vs, nullptr, 0);
        c->PSSetShader(psTonemap, nullptr, 0);
        ID3D11ShaderResourceView* srvs[2] = {r.hdr.srv, blackTex.srv};
        c->PSSetShaderResources(0, 2, srvs);
        c->Draw(3, 0);
        c->PSSetShaderResources(0, 2, nullSrv);
    }
};

}  // namespace Render
