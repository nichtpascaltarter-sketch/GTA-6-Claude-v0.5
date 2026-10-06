// Scratch-only build aid (not part of the repository): lets the vehicle test harness link while
// src/world/facadedetail.cpp is being written by the world agent.
namespace World {
void buildFacadeDetail(const Building&, const FacadeGPU&, const WorldMap&, vec3, MeshData&, std::vector<CollisionBox>*, std::vector<PropInstance>*,
                       std::vector<LightInstance>*, const std::vector<FacadeMass>&) {}
}  // namespace World
