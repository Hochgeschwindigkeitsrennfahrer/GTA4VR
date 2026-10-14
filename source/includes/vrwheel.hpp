#pragma once
#include "vrmath.hpp"
#include <vector>
#include <cstdint>
#include <algorithm>
#include <numeric>
#include <map>

namespace VRWheel
{
    struct Fit {VRMath::Matrix frame=VRMath::Identity();float radius=0;unsigned support=0;};
    // Select whole welded, slender control components, never a slice of the
    // adjoining cockpit. The pilot seat establishes a model-relative search.
    inline std::vector<unsigned char> SelectCyclic(const std::vector<VRMath::Vector>& points,
        const std::vector<unsigned>& indices,const VRMath::Vector& seat,Fit* fit=nullptr)
    {
        std::vector<unsigned char> selected(points.size());
        std::vector<unsigned> parent(points.size());std::iota(parent.begin(),parent.end(),0);
        auto root=[&](unsigned i){while(parent[i]!=i){parent[i]=parent[parent[i]];i=parent[i];}return i;};
        std::map<std::array<int,3>,unsigned> welded;
        for(unsigned i=0;i<points.size();++i)
        {
            const auto& p=points[i];
            const std::array<int,3> key{int(std::round(p[0]*10000)),int(std::round(p[1]*10000)),int(std::round(p[2]*10000))};
            auto [it,added]=welded.emplace(key,i);if(!added)parent[root(i)]=root(it->second);
        }
        for(unsigned i=0;i+2<indices.size();i+=3)
        {
            if(indices[i]>=points.size() || indices[i+1]>=points.size() || indices[i+2]>=points.size())return selected;
            parent[root(indices[i])]=root(indices[i+1]);parent[root(indices[i])]=root(indices[i+2]);
        }
        struct Part {VRMath::Vector lo,hi;std::vector<unsigned> vertices;};
        std::map<unsigned,Part> parts;
        for(unsigned i=0;i<points.size();++i)
        {
            auto [it,added]=parts.try_emplace(root(i),Part{points[i],points[i],{}});
            auto& part=it->second;part.vertices.push_back(i);
            for(unsigned a=0;a<3;++a){part.lo[a]=std::min(part.lo[a],points[i][a]);part.hi[a]=std::max(part.hi[a],points[i][a]);}
        }
        const Part* shaft=nullptr;
        for(const auto& [id,part]:parts)
        {
            const auto& lo=part.lo;const auto& hi=part.hi;
            if(hi[0]-lo[0]>.16f || hi[1]-lo[1]>.4f || hi[2]-lo[2]<.3f || hi[2]-lo[2]>.85f ||
                std::abs((lo[0]+hi[0])*.5f-seat[0])>.22f || lo[1]<seat[1]+.15f || hi[1]>seat[1]+.85f ||
                lo[2]<seat[2]-.55f || hi[2]>seat[2]+.65f)continue;
            if(!shaft || hi[2]-lo[2]>shaft->hi[2]-shaft->lo[2])shaft=&part;
        }
        if(!shaft)return selected;
        auto endpoint=[&](bool upper)
        {
            VRMath::Vector p{0,0,0,1};unsigned count=0;
            for(auto i:shaft->vertices)
                if(upper ? points[i][2]>=shaft->hi[2]-.015f:points[i][2]<=shaft->lo[2]+.015f)
                {for(unsigned a=0;a<3;++a)p[a]+=points[i][a];++count;}
            for(unsigned a=0;a<3;++a)p[a]/=count;
            return p;
        };
        const auto base=endpoint(false),top=endpoint(true);
        VRMath::Vector handle=top;unsigned handleCount=0;
        for(const auto& [id,part]:parts)
        {
            if(&part!=shaft)
            {
                if(part.hi[0]-part.lo[0]>.16f || part.hi[1]-part.lo[1]>.2f || part.hi[2]-part.lo[2]>.3f)continue;
                // Proximity to the shaft's actual upper end, rather than its
                // entire bounding box, excludes nearby dashboard instruments.
                float nearest=1.e10f;
                for(auto i:part.vertices)nearest=std::min(nearest,VRMath::Dot3(VRMath::Difference3(points[i],top),VRMath::Difference3(points[i],top)));
                if(nearest>.065f*.065f || part.hi[2]<top[2])continue;
                for(unsigned a=0;a<3;++a)handle[a]+=(part.lo[a]+part.hi[a])*.5f;
                ++handleCount;
            }
            for(auto i:part.vertices)selected[i]=1;
        }
        if(handleCount)for(unsigned a=0;a<3;++a)handle[a]=(handle[a]-top[a])/handleCount;
        const auto direction=VRMath::Difference3(handle,base);const float length=VRMath::Length3(direction);
        if(fit && length>1.e-5f)
        {
            auto unit=direction;for(unsigned a=0;a<3;++a)unit[a]/=length;
            fit->frame=VRMath::SegmentRotation({0,0,1,0},unit);fit->frame[3]=base;
            fit->radius=length;fit->support=unsigned(std::count(selected.begin(),selected.end(),1));
        }
        return selected;
    }
    // Recognize a circular rim near the authored driver's seat, rather than
    // assuming the seated animation's hand midpoint is the wheel centre.
    inline Fit FindRim(const std::vector<VRMath::Vector>& vertices,const VRMath::Vector& seat)
    {
        Fit best;
        for(unsigned search=0;search<2;++search)
        {
        if(search && best.support)break;
        for(int degrees=0;degrees<=(search ? 80:40);degrees+=5)
        {
            const float t=degrees*.017453293f,s=std::sin(t),c=std::cos(t);
            std::vector<VRMath::Vector> points;
            for(const auto& p:vertices)
                if(std::abs(p[0]-seat[0])<(search ? 1.2f:.32f) && p[1]-seat[1]>.1f && p[1]-seat[1]<(search ? 1.8f:.65f) &&
                    p[2]-seat[2]>(search ? -.6f:.05f) && p[2]-seat[2]<(search ? 1.4f:.65f))
                    points.push_back({p[0],p[1]*s+p[2]*c,-p[1]*c+p[2]*s,1});
            if(points.size()<24) continue;
            uint32_t seed=1;
            auto sample=[&]() {seed=seed*1664525u+1013904223u;return points[seed%points.size()];};
            for(unsigned trial=0;trial<512;++trial)
            {
                const auto a=sample(),b=sample(),d=sample();
                const float determinant=2*(a[0]*(b[1]-d[1])+b[0]*(d[1]-a[1])+d[0]*(a[1]-b[1]));
                if(std::abs(determinant)<1.e-6f) continue;
                const float aa=a[0]*a[0]+a[1]*a[1],bb=b[0]*b[0]+b[1]*b[1],dd=d[0]*d[0]+d[1]*d[1];
                const float x=(aa*(b[1]-d[1])+bb*(d[1]-a[1])+dd*(a[1]-b[1]))/determinant;
                const float y=(aa*(d[0]-b[0])+bb*(a[0]-d[0])+dd*(b[0]-a[0]))/determinant;
                const float z=(a[2]+b[2]+d[2])/3,r=std::hypot(a[0]-x,a[1]-y);
                if(r<.12f || r>(search ? .6f:.24f) || std::abs(x-seat[0])>(search ? .6f:.1f)) continue;
                unsigned hits=0,bins=0;
                for(const auto& p:points)
                    if(std::abs(std::hypot(p[0]-x,p[1]-y)-r)<.015f && std::abs(p[2]-z)<.035f)
                    {++hits; bins|=1u<<(unsigned((std::atan2(p[1]-y,p[0]-x)+3.141592654f)*8/6.283185307f)%8);}
                unsigned coverage=0;for(unsigned b=0;b<8;++b) coverage+=(bins>>b)&1;
                if(coverage<7 || hits<24 || hits<=best.support) continue;
                best.frame=VRMath::Identity();
                best.frame[1]={0,s,c,0};best.frame[2]={0,-c,s,0};
                best.frame[3]={x,y*s-z*c,y*c+z*s,1};
                best.radius=r;best.support=hits;
            }
        }
        }
        return best;
    }
    inline bool OnWheel(const VRMath::Vector& position,const Fit& fit)
    {
        const auto p=VRMath::Transform(position,VRMath::InverseRigid(fit.frame));
        return std::abs(p[2])<.055f && std::hypot(p[0],p[1])<fit.radius+.03f;
    }

    struct Selection {Fit fit;std::vector<unsigned char> vertices;unsigned count=0;float low=0,high=0,maxRadius=0;};
    // A dashboard may contain circles too. Fit only self-contained triangle
    // components, then include coplanar hub/spoke components in the same disc.
    // Weld position duplicates so UV/normal seams do not split the rim.
    template<class Index>
    inline Selection SelectMesh(const std::vector<VRMath::Vector>& points,
        const std::vector<Index>& indices,const VRMath::Vector& seat,const Selection* seed=nullptr)
    {
        Selection result;
        if(points.empty() || indices.empty() || indices.size()%3) return result;
        std::vector<unsigned> parents(points.size());
        std::iota(parents.begin(),parents.end(),0);
        auto root=[&](unsigned i) {while(parents[i]!=i) {parents[i]=parents[parents[i]];i=parents[i];}return i;};
        auto join=[&](unsigned a,unsigned b) {parents[root(a)]=root(b);};
        for(unsigned t=0;t<indices.size();t+=3)
        {
            const unsigned a=indices[t],b=indices[t+1],c=indices[t+2];
            if(a>=points.size() || b>=points.size() || c>=points.size()) return result;
            join(a,b);join(b,c);
        }
        std::map<std::array<int64_t,3>,unsigned> welded;
        for(unsigned i=0;i<points.size();++i)
        {
            const auto& p=points[i];
            std::array<int64_t,3> key{};
            for(unsigned a=0;a<3;++a)
            {
                if(!std::isfinite(p[a]) || std::abs(double(p[a])*10000)>double(INT64_MAX)/2) return result;
                key[a]=std::llround(double(p[a])*10000);
            }
            const auto [it,inserted]=welded.emplace(key,i);
            if(!inserted) join(i,it->second);
        }
        std::map<unsigned,std::vector<unsigned>> components;
        for(unsigned i=0;i<points.size();++i) components[root(i)].push_back(i);
        if(seed)
        {
            if(seed->vertices.size()!=points.size()) return result;
            result=*seed;
        }
        float low=result.low,high=result.high,maxRadius=result.maxRadius;
        if(!seed) for(const auto& [id,component]:components)
        {
            if(component.size()<24) continue;
            std::vector<VRMath::Vector> part;part.reserve(component.size());
            for(auto i:component) part.push_back(points[i]);
            const auto fit=FindRim(part,seat);
            if(fit.support<=result.fit.support) continue;
            const auto inverse=VRMath::InverseRigid(fit.frame);
            float zLow=INFINITY,zHigh=-INFINITY,radius=0;
            for(const auto& p:part)
            {
                const auto q=VRMath::Transform(p,inverse);
                zLow=std::min(zLow,q[2]);zHigh=std::max(zHigh,q[2]);
                radius=std::max(radius,std::hypot(q[0],q[1]));
            }
            if(zLow<-.055f || zHigh>.055f || radius>fit.radius+.045f) continue;
            result.fit=fit;low=zLow-.008f;high=zHigh+.008f;maxRadius=radius+.008f;
        }
        if(!result.fit.support) return result;
        result.low=low;result.high=high;result.maxRadius=maxRadius;
        result.vertices.resize(points.size());
        const auto inverse=VRMath::InverseRigid(result.fit.frame);
        for(const auto& [id,component]:components)
        {
            bool inside=true;
            for(auto i:component)
            {
                const auto q=VRMath::Transform(points[i],inverse);
                if(q[2]<low || q[2]>high || std::hypot(q[0],q[1])>maxRadius) {inside=false;break;}
            }
            if(inside) for(auto i:component) if(!result.vertices[i]) {result.vertices[i]=1;++result.count;}
        }
        // Dished spokes/horn pads extend behind the rim. Grow from the wheel
        // through nearby components inside a tapered centre envelope, rather
        // than widening the entire slab into the stalks/dashboard.
        bool changed=true;
        while(changed)
        {
            changed=false;
            for(const auto& [id,component]:components)
            {
                if(std::all_of(component.begin(),component.end(),[&](unsigned i){return result.vertices[i]!=0;})) continue;
                bool inside=true;
                for(auto i:component)
                {
                    const auto q=VRMath::Transform(points[i],inverse);
                    const float radius=std::hypot(q[0],q[1]);
                    const float dish=result.fit.radius*.75f*(1.f-std::min(1.f,radius/maxRadius));
                    if(radius>maxRadius || q[2]<low-dish || q[2]>high+dish) {inside=false;break;}
                }
                if(!inside) continue;
                bool touches=false;
                for(auto i:component)
                {
                    for(unsigned j=0;j<points.size();++j) if(result.vertices[j])
                    {
                        float distance=0;
                        for(unsigned a=0;a<3;++a) {const float delta=points[i][a]-points[j][a];distance+=delta*delta;}
                        if(distance<=.025f*.025f) {touches=true;break;}
                    }
                    if(touches)break;
                }
                if(touches)
                {
                    for(auto i:component) if(!result.vertices[i]) {result.vertices[i]=1;++result.count;}
                    changed=true;
                }
            }
        }
        return result;
    }
}
