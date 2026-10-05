#pragma once
#include <regex>
#include <sstream>
#include <string>

namespace VRBillboardShader
{
    // Build a prefix around a runtime shader, retaining its occlusion/fog logic.
    // This is a prototype helper; no native shader or bytecode is distributed.
    // Inputs: v0=unit corner, v3=center, v4/v5=half axes; v1/v2 stay native.
    inline bool AddInstanceExpansion(const std::string& assembly,std::string& output,bool compactRecord=false)
    {
        output.clear();
        if(assembly.size()>65536) return false;
        const std::regex registerToken(R"(\b([vr])(\d+)\b)");
        const std::regex positionToken(R"(\bv0\b)");
        std::istringstream input(assembly);
        std::string line,body;
        bool version=false,position=false,center=false,inserted=false,zeroDefinition=false;
        while(std::getline(input,line))
        {
            const auto first=line.find_first_not_of(" \t\r");
            const auto instruction=first==std::string::npos ? std::string{}:line.substr(first);
            const bool comment=instruction.empty() || instruction.starts_with("//");
            if(!comment)
            {
                for(std::sregex_iterator i(line.begin(),line.end(),registerToken),end;i!=end;++i)
                {
                    const auto index=std::stoi((*i)[2].str());
                    if(((*i)[1]=="v" && index>3) || ((*i)[1]=="r" && index>=(compactRecord ? 26:30))) return false;
                }
                if(instruction.starts_with("vs_3_0")) version=true;
                if(instruction.starts_with("dcl_position v0")) position=true;
                if(instruction.starts_with("dcl_normal v3")) center=true;
                if(std::regex_search(instruction,std::regex(R"(^def\s+c17,\s*1,\s*0,)"))) zeroDefinition=true;
                const bool declaration=instruction.starts_with("dcl_");
                const bool definition=instruction.starts_with("def ") || instruction.starts_with("defi ") || instruction.starts_with("defb ");
                if(!declaration && !definition && !instruction.starts_with("vs_3_0"))
                {
                    if(!inserted)
                    {
                        if(!version || !position || !center || (compactRecord && !zeroDefinition)) return false;
                        body+="    dcl_texcoord1 v4\n    dcl_texcoord2 v5\n";
                        // SM3 permits only one distinct input register per instruction.
                        if(compactRecord)
                        {
                            // v2=(radius,brightness,push); raw native RGB bytes in v1.
                            // v0=(corner signs,atlas U,atlas V); v4/v5 are unit axes.
                            body+="    mov r29, v3\n    add r26.xyz, v3, -c15\n";
                            body+="    dp3 r26.w, r26, r26\n    if_gt r26.w, c17.y\n";
                            body+="      rsq r26.w, r26.w\n      mul r26.xyz, r26, r26.w\n";
                            body+="      mov r26.w, v2.z\n      mad r29.xyz, -r26.w, r26, r29\n    endif\n";
                            body+="    mov r27.yz, v2.yyxx\n    mov r27.x, v0.z\n";
                            body+="    mov r28.xyz, v1.zyx\n    mov r28.w, v0.w\n";
                            body+="    mov r31.zw, v0.xxxy\n    mov r30.xyz, v4\n";
                            body+="    mul r30.xyz, r27.z, r30\n    mad r30.xyz, r31.z, r30, r29\n";
                            body+="    mov r31.xyz, v5\n    mul r31.xyz, r27.z, r31\n";
                            body+="    mad r31.xyz, r31.w, r31, r30\n    mov r31.w, v3.w\n";
                        }
                        else
                        {
                        body+="    mov r31.zw, v0.xxxy\n    mov r30.xyz, v4\n";
                        body+="    mad r30.xyz, r31.z, r30, v3\n    mov r31.xyz, v5\n";
                        body+="    mad r31.xyz, r31.w, r31, r30\n";
                        body+="    mov r31.w, v3.w\n";
                        }
                        inserted=true;
                    }
                    line=std::regex_replace(line,positionToken,"r31");
                    if(compactRecord)
                    {
                        line=std::regex_replace(line,std::regex(R"(\bv1\b)"),"r28");
                        line=std::regex_replace(line,std::regex(R"(\bv2\b)"),"r27");
                        line=std::regex_replace(line,std::regex(R"(\bv3\b)"),"r29");
                    }
                }
                else if(inserted) return false; // Refuse unsupported declaration ordering.
            }
            body+=line+'\n';
        }
        if(!inserted) return false;
        output=std::move(body);
        return true;
    }
}
