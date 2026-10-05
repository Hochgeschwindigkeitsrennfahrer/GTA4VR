#pragma once
#include <array>
#include <regex>
#include <sstream>
#include <string>

namespace VRRigidInstanceShader
{
    // Prototype for a verified rigid SM3 variant: carry world/clip matrices as
    // instance attributes, retaining the runtime native shader's full body.
    // Material/fade/animation/state compatibility is the caller's responsibility.
    inline bool AddInstanceMatrices(const std::string& assembly,std::string& output,bool tangent=false)
    {
        output.clear();
        if(assembly.size()>65536 || std::regex_search(assembly,std::regex(R"(\bc\d*\s*\[)"))) return false;
        constexpr std::array<unsigned,8> constants{0,1,2,3,8,9,10,11};
        const std::regex registers(R"(\b([vr])(\d+))");
        std::istringstream input(assembly);
        std::string line,body;
        bool version=false,inserted=false;
        unsigned inputs=0;
        while(std::getline(input,line))
        {
            const auto first=line.find_first_not_of(" \t\r");
            const auto instruction=first==std::string::npos ? std::string{}:line.substr(first);
            if(!instruction.empty() && !instruction.starts_with("//"))
            {
                for(std::sregex_iterator i(line.begin(),line.end(),registers),end;i!=end;++i)
                {
                    const auto index=std::stoi((*i)[2].str());
                    if(((*i)[1]=="v" && index>(tangent ? 4:3)) || ((*i)[1]=="r" && index>=16 && index<=23))return false;
                }
                if(instruction.starts_with("vs_3_0"))version=true;
                if(instruction.starts_with("dcl_position v0"))inputs|=1;
                if(instruction.starts_with("dcl_color v1"))inputs|=2;
                if(instruction.starts_with("dcl_texcoord v2"))inputs|=4;
                if(instruction.starts_with("dcl_normal v3"))inputs|=8;
                if(instruction.starts_with("dcl_tangent v4"))inputs|=16;
                const bool declaration=instruction.starts_with("dcl_");
                const bool definition=instruction.starts_with("def ") || instruction.starts_with("defi ") || instruction.starts_with("defb ");
                if(definition)
                    for(auto index:constants)
                        if(std::regex_search(instruction,std::regex("^def\\s+c"+std::to_string(index)+"\\s*,")))return false;
                if(!declaration && !definition && !instruction.starts_with("vs_3_0"))
                {
                    if(!inserted)
                    {
                        if(!version || inputs!=(tangent ? 31u:15u))return false;
                        for(unsigned i=0;i<8;++i)
                            body+="    dcl_texcoord"+std::to_string(i+1)+" v"+std::to_string(i+(tangent ? 5:4))+'\n';
                        for(unsigned i=0;i<8;++i)
                            body+="    mov r"+std::to_string(i+16)+", v"+std::to_string(i+(tangent ? 5:4))+'\n';
                        inserted=true;
                    }
                    for(unsigned i=0;i<8;++i)
                        line=std::regex_replace(line,std::regex("\\bc"+std::to_string(constants[i])+"(?=[^0-9]|$)"),"r"+std::to_string(i+16));
                }
                else if(inserted)return false;
            }
            body+=line+'\n';
        }
        if(!inserted)return false;
        output=std::move(body);return true;
    }
}
