#include "../../source/includes/vrwheel.hpp"
#include <cassert>

int main()
{
    {
        // A shaft and separate grip share welded seams; dashboard geometry
        // touches the search region but must never be partially rebound.
        std::vector<VRMath::Vector> p{{0,.45f,-.3f,1},{.02f,.45f,-.3f,1},{0,.45f,.2f,1},
            {0,.45f,.2f,1},{.06f,.45f,.2f,1},{0,.45f,.4f,1},
            {0,.46f,.2f,1},{.8f,.46f,.2f,1},{0,.46f,.5f,1}};
        // Small instrument lies near the middle of the shaft bounding box,
        // but does not connect at its upper end.
        p.insert(p.end(),{{.01f,.5f,-.15f,1},{.05f,.5f,-.15f,1},{.01f,.5f,-.1f,1}});
        const std::vector<unsigned> indices{0,1,2,3,4,5,6,7,8,9,10,11};
        VRWheel::Fit cyclic;auto selected=VRWheel::SelectCyclic(p,indices,{0,0,0,1},&cyclic);
        for(unsigned i=0;i<6;++i)assert(selected[i]);
        for(unsigned i=6;i<12;++i)assert(!selected[i]);
        assert(std::abs(cyclic.frame[3][2]+.3f)<1.e-5f);
        const auto tilt=VRMath::SegmentRotation({0,0,1,0},{0,.6f,.8f,0});
        const auto delta=VRMath::Multiply(VRMath::Multiply(VRMath::InverseRigid(cyclic.frame),tilt),cyclic.frame);
        const auto fixed=VRMath::Transform(cyclic.frame[3],delta);
        assert(VRMath::Length3(VRMath::Difference3(fixed,cyclic.frame[3]))<1.e-5f);
        assert(VRWheel::SelectCyclic({}, {}, {0,0,0,1}).empty());
    }

    std::vector<VRMath::Vector> points;
    std::vector<unsigned short> indices;
    // Complete wheel with split UV seams and an independently authored hub.
    for(unsigned i=0;i<64;++i)
    {
        const float angle=i*6.283185307f/64;
        for(float radius:{.17f,.18f}) points.push_back({radius*std::cos(angle),.4f,.4f+radius*std::sin(angle),1});
        const unsigned short a=2*i,b=(2*i+2)%128;
        indices.insert(indices.end(),{a,static_cast<unsigned short>(a+1),b,b,static_cast<unsigned short>(a+1),static_cast<unsigned short>(b+1)});
    }
    const unsigned wheelSize=points.size();
    for(const auto& p:std::vector<VRMath::Vector>{{-.04f,.4f,.37f,1},{.04f,.4f,.37f,1},{0,.4f,.44f,1}}) points.push_back(p);
    indices.insert(indices.end(),{128,129,130});
    // Adjacent dashboard overlaps the old selection disc, but extends outside
    // it. The entire component must stay fixed, including its nearby vertex.
    points.insert(points.end(),{{.01f,.4f,.42f,1},{.7f,.4f,.42f,1},{.7f,.4f,.7f,1}});
    indices.insert(indices.end(),{131,132,133});
    // A stalk behind the rim is inside the old 5.5cm slab but is not wheel mesh.
    points.insert(points.end(),{{-.1f,.44f,.4f,1},{-.08f,.44f,.4f,1},{-.1f,.44f,.42f,1}});
    indices.insert(indices.end(),{134,135,136});
    const auto selected=VRWheel::SelectMesh(points,indices,{0,0,0,1});
    assert(selected.fit.support>=40 && selected.count==wheelSize+3);
    for(unsigned i=0;i<points.size();++i) assert(bool(selected.vertices[i])==(i<wheelSize+3));
    // A taller authored seat origin must not cut away the lower truck rim.
    auto lowWheel=points;for(auto& p:lowWheel)p[2]-=.15f;
    const auto lowSelected=VRWheel::SelectMesh(lowWheel,indices,{0,0,0,1});
    assert(lowSelected.count==wheelSize+3);
    // A dished spoke connects to the rim and carries a separate centre badge.
    // The nearby detached stalk above must still remain stationary.
    points.insert(points.end(),{{.16f,.405f,.4f,1},{.01f,.45f,.4f,1},{.04f,.445f,.42f,1},
        {.005f,.46f,.4f,1},{-.005f,.46f,.4f,1},{.005f,.46f,.405f,1}});
    indices.insert(indices.end(),{137,138,139,140,141,142});
    const auto dished=VRWheel::SelectMesh(points,indices,{0,0,0,1});
    assert(dished.count==wheelSize+9);
    for(unsigned i=131;i<137;++i)assert(!dished.vertices[i]);
    for(unsigned i=137;i<143;++i)assert(dished.vertices[i]);
    // Assembly completion takes a seed from one material and traverses a
    // combined 32-bit index space. Existing seed vertices must not be counted
    // twice when another material welds onto the same wheel component.
    auto assemblySeed=selected;assemblySeed.vertices.resize(points.size());
    const std::vector<unsigned> assemblyIndices(indices.begin(),indices.end());
    const auto assembly=VRWheel::SelectMesh(points,assemblyIndices,{0,0,0,1},&assemblySeed);
    assert(assembly.count==dished.count && assembly.vertices==dished.vertices);
    auto larger=points;for(auto& p:larger)for(unsigned a=0;a<3;++a)p[a]*=1.2f;
    const auto largerWheel=VRWheel::SelectMesh(larger,assemblyIndices,{0,0,0,1});
    assert(largerWheel.count==dished.count);
    auto invalid=indices;invalid[0]=65535;
    assert(VRWheel::SelectMesh(points,invalid,{0,0,0,1}).count==0);
    invalid.pop_back();assert(VRWheel::SelectMesh(points,invalid,{0,0,0,1}).count==0);
    // A ship's wheel is larger than a car rim and centred below eye/seat
    // height. Fall back to the wider helm search without changing car fits.
    std::vector<VRMath::Vector> helm;
    for(unsigned i=0;i<128;++i)
    {
        const float angle=i*6.283185307f/128;
        helm.push_back({-.84f+.48f*std::cos(angle),-6.06f,1.04f+.48f*std::sin(angle),1});
    }
    const auto helmFit=VRWheel::FindRim(helm,{-.806f,-6.663f,1.02f,1});
    assert(helmFit.support>=120 && std::abs(helmFit.radius-.48f)<.01f);

    // Thick rims have sparse samples on two separate depth faces. The circle
    // fit must gather both without weakening whole-component exclusion.
    std::vector<VRMath::Vector> thick;
    for(unsigned i=0;i<16;++i)for(float radius:{.20f,.24f})for(float y:{.435f,.480f})for(unsigned seam=0;seam<2;++seam)
    {
        const float angle=i*6.283185307f/16;
        thick.push_back({radius*std::cos(angle),y,.3f+radius*std::sin(angle),1});
    }
    assert(VRWheel::FindRim(thick,{0,0,0,1}).support>=40);

    // Low-polygon rims can be split into independently indexed material faces.
    // Thirty-two vertices still describe a complete circle with broad coverage.
    std::vector<VRMath::Vector> sparse;std::vector<unsigned> sparseIndices;
    for(unsigned i=0;i<16;++i)
    {
        const float angle=i*6.283185307f/16;
        for(float radius:{.17f,.18f})sparse.push_back({radius*std::cos(angle),.4f,.4f+radius*std::sin(angle),1});
        const unsigned a=2*i,b=(a+2)%32;
        sparseIndices.insert(sparseIndices.end(),{a,a+1,b,b,a+1,b+1});
    }
    const auto sparseWheel=VRWheel::SelectMesh(sparse,sparseIndices,{0,0,0,1});
    assert(sparseWheel.fit.support>=24 && sparseWheel.count==32);

}
