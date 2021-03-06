//=============================================================================
//
// Render/MSLGenerator.cpp
//
// Created by Max McGuire (max@unknownworlds.com)
// Copyright (c) 2013, Unknown Worlds Entertainment, Inc.
//
//=============================================================================

//#include "Engine/String.h"
//#include "Engine/Log.h"
#include "Engine.h"

#include "MSLGenerator.h"
#include "HLSLParser.h"
#include "HLSLTree.h"

// Things that are not supported:
// - Passing swizzled expressions as out or inout arguments. Out arguments are passed by reference in C++, but
//   swizzled expressions are not addressable.
// - Matrix [] access is implemented as a function call, so result cannot be passed as out/inout argument.
// - Matrix [] access is not supported in all l-value expressions. Only simple assignments.
// - Some type conversions and constructors don't work exactly the same way. For example, casts to smaller size vectors are not alloweed in C++. @@ Add more details...
// - No support for boolean vectors and logical operators involving vectors. This is not just in metal.
// - No general support for uniform buffers.
// - What else?

namespace M4
{
    
static const char* GetTypeName(const HLSLType& type)
{
    // ACoget-TODO: How to detect non-float textures, if relevant?
    switch (type.baseType)
    {
    case HLSLBaseType_Void:             return "void";
    case HLSLBaseType_Float:            return "float";
    case HLSLBaseType_Float2:           return "float2";
    case HLSLBaseType_Float3:           return "float3";
    case HLSLBaseType_Float4:           return "float4";
    case HLSLBaseType_Float2x2:         return "float2x2";
    case HLSLBaseType_Float3x3:         return "float3x3";
    case HLSLBaseType_Float4x4:         return "float4x4";
    case HLSLBaseType_Float4x3:         return "float3x4";
    case HLSLBaseType_Float4x2:         return "float2x4";
    case HLSLBaseType_Half:             return "half";
    case HLSLBaseType_Half2:            return "half2";
    case HLSLBaseType_Half3:            return "half3";
    case HLSLBaseType_Half4:            return "half4";
    case HLSLBaseType_Half2x2:          return "half2x2";
    case HLSLBaseType_Half3x3:          return "half3x3";
    case HLSLBaseType_Half4x4:          return "half4x4";
    case HLSLBaseType_Half4x3:          return "half3x4";
    case HLSLBaseType_Half4x2:          return "half2x4";
    case HLSLBaseType_Bool:             return "bool";
    case HLSLBaseType_Bool2:            return "bool2";
    case HLSLBaseType_Bool3:            return "bool3";
    case HLSLBaseType_Bool4:            return "bool4";
    case HLSLBaseType_Int:              return "int";
    case HLSLBaseType_Int2:             return "int2";
    case HLSLBaseType_Int3:             return "int3";
    case HLSLBaseType_Int4:             return "int4";
    case HLSLBaseType_Uint:             return "uint";
    case HLSLBaseType_Uint2:            return "uint2";
    case HLSLBaseType_Uint3:            return "uint3";
    case HLSLBaseType_Uint4:            return "uint4";
    case HLSLBaseType_Texture:          return "texture";
    case HLSLBaseType_Sampler:          return "sampler";
    case HLSLBaseType_Sampler2D:        return "texture2d<float>";
    case HLSLBaseType_Sampler3D:        return "texture3d<float>";
    case HLSLBaseType_SamplerCube:      return "texturecube<float>";
    case HLSLBaseType_Sampler2DShadow:  return "depth2d<float>";
    case HLSLBaseType_Sampler2DMS:      return "texture2d_ms<float>";
    case HLSLBaseType_UserDefined:      return type.typeName;
    default:
        ASSERT(0);
        return "<unknown type>";
    }
}

static bool is_semantic(const char * semantic, const char * name, int index)
{
    const char * s = semantic;
    int n = strlen(name);

    if (strncmp(s, name, n) != 0)
    {
        // Name doesn't match.
        return false;
    }
    s += n;

    if (s[0] - '0' == index) {
        s++;
    }
    else if (index != 0)
    {
        // Index doesn't match.
        return false;
    }

    // EOS doesn't match.
    return s[0] == '\0';
}
static bool is_semantic(const char * semantic, const char * name)
{
    return strcmp(semantic, name) == 0;
}

static const char * translate_input_semantic(MSLGenerator::Target target, const char * semantic)
{
    if (semantic != NULL)
    {
        if (target == MSLGenerator::Target_VertexShader)
        {
            if (is_semantic(semantic, "POSITION", 0)) return "attribute(0)";
            if (is_semantic(semantic, "TEXCOORD", 0)) return "attribute(1)";
            if (is_semantic(semantic, "TEXCOORD", 1)) return "attribute(2)";
            if (is_semantic(semantic, "NORMAL", 0)) return "attribute(3)";
            if (is_semantic(semantic, "TANGENT", 0)) return "attribute(4)";
            if (is_semantic(semantic, "COLOR", 0)) return "attribute(5)";
            if (is_semantic(semantic, "COLOR", 1)) return "attribute(6)";
            if (is_semantic(semantic, "BLENDINDICES", 0)) return "attribute(7)";
            if (is_semantic(semantic, "BLENDWEIGHT", 0)) return "attribute(8)";
            if (is_semantic(semantic, "INSTANCE_ID")) return "instance_id";
            if (is_semantic(semantic, "VERTEX_ID")) return "vertex_id";
        }
        else if (target == MSLGenerator::Target_FragmentShader)
        {
            if (is_semantic(semantic, "POSITION", 0)) return "position";
            //if (is_semantic(semantic, "VPOS")) return "position";
            if (is_semantic(semantic, "VFACE")) return "front_facing";
            //if (is_semantic(semantic, "COLOR", 0)) return "color(0)";      // For programmable blending.
        }
    }
    return NULL;
}

static const char * translate_output_semantic(MSLGenerator::Target target, const char * semantic)
{
    if (semantic != NULL)
    {
        if (target == MSLGenerator::Target_VertexShader)
        {
            if (is_semantic(semantic, "POSITION", 0)) return "position";
        }
        else if (target == MSLGenerator::Target_FragmentShader)
        {
            if (is_semantic(semantic, "COLOR", 0)) return "color(0)";
            if (is_semantic(semantic, "COLOR", 1)) return "color(1)";
            if (is_semantic(semantic, "DEPTH")) return "depth(any)";
            if (is_semantic(semantic, "DEPTH_GT")) return "depth(greater)";
            if (is_semantic(semantic, "DEPTH_LT")) return "depth(less)";
            if (is_semantic(semantic, "SAMPLE_MASK", 0)) return "sample_mask";
        }
    }
    return NULL;
}


MSLGenerator::MSLGenerator()
{
    m_tree                          = NULL;
    m_entryName                     = NULL;
    m_target                        = Target_VertexShader;
    m_error = false;
}

// Copied from GLSLGenerator
void MSLGenerator::Error(const char* format, ...)
{
    // It's not always convenient to stop executing when an error occurs,
    // so just track once we've hit an error and stop reporting them until
    // we successfully bail out of execution.
    if (m_error)
    {
        return;
    }
    m_error = true;
    
    va_list arg;
    va_start(arg, format);
    Log_ErrorArgList(format, arg);
    va_end(arg);
}

inline void MSLGenerator::AddClassArgument(ClassArgument * arg)
{
    if (m_firstClassArgument == NULL)
    {
        m_firstClassArgument = arg;
    }
    else
    {
        m_lastClassArgument->nextArg = arg;
    }
    m_lastClassArgument = arg;
}

void MSLGenerator::Prepass(HLSLTree* tree, Target target, HLSLFunction* entryFunction)
{
    // ACoget-TODO: process uniforms
    // ACoget-TODO: list used texture types to trim functions being prepended
    
    // Hide unused arguments. @@ It would be good to do this in the generator too.
    HideUnusedArguments(entryFunction);

    // Collect textures, in order with names
    HLSLRoot* root = tree->GetRoot();
    HLSLStatement* statement = root->statement;
    ASSERT(m_firstClassArgument == NULL);

    int textureIndex = 0;

    // @@ IC: Hack! LIST BUFFERS IN THE RIGHT order.
    int bufferOffset = 0;
    if (target == Target_VertexShader) {
        bufferOffset = 1; // Index 0 reserved for input vertex buffer.
    }

    HLSLType samplerType(HLSLBaseType_Sampler);

    ClassArgument * currentArg = m_firstClassArgument;
    while (statement != NULL) {
        HLSLStatement* nextStatement = statement->nextStatement;
        
        if (statement->nodeType == HLSLNodeType_Declaration) {
            HLSLDeclaration* declaration = (HLSLDeclaration*)statement;
            
            if (!declaration->hidden && IsSamplerType(declaration->type))
            {
                // We just want to list textures in order, no semantic handling is necessary
                const char * textureName = m_tree->AddStringFormat("%s_texture", declaration->name);
                const char * textureRegisterName = m_tree->AddStringFormat("texture(%d)", textureIndex);
                AddClassArgument(new ClassArgument(textureName, declaration->type, textureRegisterName));

                if (declaration->type.baseType != HLSLBaseType_Sampler2DMS)
                {
                    const char * samplerName = m_tree->AddStringFormat("%s_sampler", declaration->name);
                    const char * samplerRegisterName = m_tree->AddStringFormat("sampler(%d)", textureIndex);
                    AddClassArgument(new ClassArgument(samplerName, samplerType, samplerRegisterName));
                }

                textureIndex++;
            }
        }
        else if (statement->nodeType == HLSLNodeType_Buffer) {
            HLSLBuffer * buffer = (HLSLBuffer *)statement;

            if (!buffer->hidden) {
                HLSLType type(HLSLBaseType_UserDefined);
                type.addressSpace = HLSLAddressSpace_Constant;
                type.typeName = m_tree->AddStringFormat("Uniforms_%s", buffer->name);

                int bufferIndex = -1;
                if (strcmp(buffer->name, "per_pass") == 0) bufferIndex = bufferOffset + 0;
                else if (strcmp(buffer->name, "per_item") == 0) bufferIndex = bufferOffset + 1;

                const char * registerName = m_tree->AddStringFormat("buffer(%d)", bufferIndex);

                if (bufferIndex >= 0)
                {
                    AddClassArgument(new ClassArgument(buffer->name, type, registerName));
                }

                bufferIndex--;
            }
        }
        
        statement = statement->nextStatement;
    }
    
    // @@ IC: instance_id parameter must be a function argument. If we find it inside a struct we must move it to the function arguments
    // and patch all the references to it!
    
    // Translate semantics.
    HLSLArgument * argument = entryFunction->argument;
    while (argument != NULL)
    {
        if (argument->hidden) {
            argument = argument->nextArgument;
            continue;
        }

        if (argument->modifier == HLSLArgumentModifier_Out)
        {
            // Translate output arguments semantics.
            if (argument->type.baseType == HLSLBaseType_UserDefined)
            {
                // Our vertex input is a struct and its fields need to be tagged when we generate that
                HLSLStruct * structure = tree->FindGlobalStruct(argument->type.typeName);
                if (structure == NULL) {
                    Error("Vertex shader output struct '%s' not found in shader\n", argument->type.typeName);
                }

                HLSLStructField* field = structure->field;
                while (field != NULL)
                {
                    if (!field->hidden)
                    {
                        field->sv_semantic = translate_output_semantic(target, field->semantic);
                    }
                    field = field->nextField;
                }
            }
            else
            {
                argument->sv_semantic = translate_output_semantic(target, argument->semantic);
            }
        }
        else {
            // Translate input arguments semantics.
            if (argument->type.baseType == HLSLBaseType_UserDefined)
            {
                // Our vertex input is a struct and its fields need to be tagged when we generate that
                HLSLStruct * structure = tree->FindGlobalStruct(argument->type.typeName);
                if (structure == NULL) {
                    Error("Vertex shader input struct '%s' not found in shader\n", argument->type.typeName);
                }

                HLSLStructField* field = structure->field;
                while (field != NULL)
                {
                    // Hide vertex shader output position from fragment shader. @@ This is messing up the runtime compiler...
                    /*if (target == Target_FragmentShader && is_semantic(field->semantic, "POSITION"))
                    {
                        field->hidden = true;
                    }*/

                    if (!field->hidden)
                    {
                        field->sv_semantic = translate_input_semantic(target, field->semantic);

                        /*if (target == Target_VertexShader && is_semantic(field->semantic, "COLOR"))
                        {
                            field->type.flags |= HLSLTypeFlag_Swizzle_BGRA;
                        }*/
                    }
                    field = field->nextField;
                }
            }
            else
            {
                argument->sv_semantic = translate_input_semantic(target, argument->semantic);
            }
        }

        argument = argument->nextArgument;
    }

    // Translate return value semantic.
    if (entryFunction->returnType.baseType != HLSLBaseType_Void)
    {
        if (entryFunction->returnType.baseType == HLSLBaseType_UserDefined)
        {
            // Our vertex input is a struct and its fields need to be tagged when we generate that
            HLSLStruct * structure = tree->FindGlobalStruct(entryFunction->returnType.typeName);
            if (structure == NULL) {
                Error("Vertex shader output struct '%s' not found in shader\n", entryFunction->returnType.typeName);
            }

            HLSLStructField* field = structure->field;
            while (field != NULL)
            {
                if (!field->hidden)
                {
                    field->sv_semantic = translate_output_semantic(target, field->semantic);
                }
                field = field->nextField;
            }
        }
        else
        {
            entryFunction->sv_semantic = translate_output_semantic(target, entryFunction->semantic);
        }
    }
}

void MSLGenerator::CleanPrepass()
{
    ClassArgument * currentArg = m_firstClassArgument;
    while (currentArg != NULL)
    {
        ClassArgument * nextArg = currentArg->nextArg;
        delete currentArg;
        currentArg = nextArg;
    }
    delete currentArg;
    m_firstClassArgument = NULL;
    m_lastClassArgument = NULL;
}
    
bool MSLGenerator::Generate(HLSLTree* tree, Target target, const char* entryName, int flags)
{
    m_firstClassArgument = NULL;
    m_lastClassArgument = NULL;

    m_tree      = tree;
    m_entryName = entryName;
    m_target    = target;
    ASSERT(m_target == Target_VertexShader || m_target == Target_FragmentShader);

    m_writer.Reset();

    // Find entry point function
    HLSLFunction * entryFunction = tree->FindFunction(entryName);
    if (entryFunction == NULL)
    {
        Error("Entry point '%s' doesn't exist\n", entryName);
        return false;
    }
    
    Prepass(tree, target, entryFunction);
    
    // ACoget-TODO: replace by prepass
    m_per_pass_buffer = NULL;
    m_per_item_buffer = NULL;
    
    // ACoget-TODO: add a helper function for all the prepended code
    // ACoget-TODO: trim what gets added based on what the shader uses
    m_writer.WriteLine(0, "#include <metal_stdlib>");
    m_writer.WriteLine(0, "#include <metal_texture>");
    m_writer.WriteLine(0, "#include <metal_math>");
    m_writer.WriteLine(0, "using namespace metal;");
    m_writer.WriteLine(0, "");
    
    // 'mad' should be translated as 'fma'

    // Any special function stubs we need go here
    // That includes special constructors to emulate HLSL not being strict

    if (FindFunctionCall(entryFunction, "mad")) {
        m_writer.WriteLine(0, "inline float mad(float a, float b, float c) {");
        m_writer.WriteLine(1, "return a * b + c;");
        m_writer.WriteLine(0, "}");
        m_writer.WriteLine(0, "inline float2 mad(float2 a, float2 b, float2 c) {");
        m_writer.WriteLine(1, "return a * b + c;");
        m_writer.WriteLine(0, "}");
        m_writer.WriteLine(0, "inline float3 mad(float3 a, float3 b, float3 c) {");
        m_writer.WriteLine(1, "return a * b + c;");
        m_writer.WriteLine(0, "}");
        m_writer.WriteLine(0, "inline float4 mad(float4 a, float4 b, float4 c) {");
        m_writer.WriteLine(1, "return a * b + c;");
        m_writer.WriteLine(0, "}");
    }

    if (FindFunctionCall(entryFunction, "max")) {
        m_writer.WriteLine(0, "inline float max(int a, float b) {");
        m_writer.WriteLine(1, "return max((float)a, b);");
        m_writer.WriteLine(0, "}");
        m_writer.WriteLine(0, "inline float max(float a, int b) {");
        m_writer.WriteLine(1, "return max(a, (float)b);");
        m_writer.WriteLine(0, "}");
    }
    if (FindFunctionCall(entryFunction, "min")) {
        m_writer.WriteLine(0, "inline float min(int a, float b) {");
        m_writer.WriteLine(1, "return min((float)a, b);");
        m_writer.WriteLine(0, "}");
        m_writer.WriteLine(0, "inline float min(float a, int b) {");
        m_writer.WriteLine(1, "return min(a, (float)b);");
        m_writer.WriteLine(0, "}");
    }

    if (FindFunctionCall(entryFunction, "lerp")) {
        m_writer.WriteLine(0, "template<typename T> inline T mix(T a, T b, int x) {");
        m_writer.WriteLine(1, "return mix(a, b, (float)x);");
        m_writer.WriteLine(0, "}");
        m_writer.WriteLine(0, "#define lerp mix");
    }

    if (FindFunctionCall(entryFunction, "mul")) {
        // @@ Add all mul variants? Replace by * ?
        m_writer.WriteLine(0, "inline float4 mul(float4 a, float4x4 m) {");
        m_writer.WriteLine(1, "return a * m;");
        m_writer.WriteLine(0, "}");

        m_writer.WriteLine(0, "inline float3 mul(float4 a, float3x4 m) {");
        m_writer.WriteLine(1, "return a * m;");
        m_writer.WriteLine(0, "}");
    }

    // @@ How do we know if these will be needed? We could write these after parsing the whole file and prepend them.
    m_writer.WriteLine(0, "inline float4 column(float4x4 m, int i) {");
    m_writer.WriteLine(1, "return float4(m[0][i], m[1][i], m[2][i], m[3][i]);");
    m_writer.WriteLine(0, "}");

    m_writer.WriteLine(0, "inline float3 column(float3x4 m, int i) {");
    m_writer.WriteLine(1, "return float3(m[0][i], m[1][i], m[2][i]);");
    m_writer.WriteLine(0, "}");

    m_writer.WriteLine(0, "inline float2 column(float2x4 m, int i) {");
    m_writer.WriteLine(1, "return float2(m[0][i], m[1][i]);");
    m_writer.WriteLine(0, "}");

    m_writer.WriteLine(0, "inline float4 set_column(thread float4x4& m, int i, float4 v) {");
    m_writer.WriteLine(1, "    m[0][i] = v.x; m[1][i] = v.y; m[2][i] = v.z; m[3][i] = v.w; return v;");
    m_writer.WriteLine(0, "}");

    m_writer.WriteLine(0, "inline float3 set_column(thread float3x4& m, int i, float3 v) {");
    m_writer.WriteLine(1, "    m[0][i] = v.x; m[1][i] = v.y; m[2][i] = v.z; return v;");
    m_writer.WriteLine(0, "}");

    m_writer.WriteLine(0, "inline float2 set_column(thread float2x4& m, int i, float2 v) {");
    m_writer.WriteLine(1, "    m[0][i] = v.x; m[1][i] = v.y; return v;");
    m_writer.WriteLine(0, "}");


    if (FindFunctionCall(entryFunction, "clip"))
    {
        m_writer.WriteLine(0, "inline void clip(float x) {");
        m_writer.WriteLine(1, "if (x < 0.0) discard_fragment();");
        m_writer.WriteLine(0, "}");
    }
    if (FindFunctionCall(entryFunction, "rcp"))
    {
        m_writer.WriteLine(0, "inline float rcp(float x) {");
        m_writer.WriteLine(1, "return 1.0f / x;");
        m_writer.WriteLine(0, "}");
    }

    if (FindFunctionCall(entryFunction, "ddx")) m_writer.WriteLine(0, "#define ddx dfdx");
    if (FindFunctionCall(entryFunction, "ddy")) m_writer.WriteLine(0, "#define ddy dfdy");
    if (FindFunctionCall(entryFunction, "frac")) m_writer.WriteLine(0, "#define frac fract");
    
    //m_writer.WriteLine(0, "#define mad fma");     // @@ This doesn't seem to work.
    
    if (FindFunctionCall(entryFunction, "tex2D") ||
        FindFunctionCall(entryFunction, "tex2Dlod") ||
        FindFunctionCall(entryFunction, "tex2Dgrad") ||
        FindFunctionCall(entryFunction, "tex2Dbias"))
    {
        m_writer.WriteLine(0, "struct Texture2DSampler {");
        m_writer.WriteLine(1, "const thread texture2d<float>& t;");
        m_writer.WriteLine(1, "const thread sampler& s;");
        m_writer.WriteLine(1, "Texture2DSampler(thread const texture2d<float>& t, thread const sampler& s) : t(t), s(s) {};");
        m_writer.WriteLine(0, "};");
    }

    if (FindFunctionCall(entryFunction, "tex2D"))
    {
        m_writer.WriteLine(0, "inline float4 tex2D(Texture2DSampler ts, float2 texCoord) {");
        m_writer.WriteLine(1, "return ts.t.sample(ts.s, texCoord);");
        m_writer.WriteLine(0, "}");
    }
    if (FindFunctionCall(entryFunction, "tex2Dlod"))
    {
        m_writer.WriteLine(0, "inline float4 tex2Dlod(Texture2DSampler ts, float4 texCoordMip) {");
        m_writer.WriteLine(1, "return ts.t.sample(ts.s, texCoordMip.xy, level(texCoordMip.w));");
        m_writer.WriteLine(0, "}");
    }
    if (FindFunctionCall(entryFunction, "tex2Dgrad"))
    {
        m_writer.WriteLine(0, "inline float4 tex2Dgrad(Texture2DSampler ts, float2 texCoord, float2 gradx, float2 grady) {");
        m_writer.WriteLine(1, "return ts.t.sample(ts.s, texCoord.xy, gradient2d(gradx, grady));");
        m_writer.WriteLine(0, "}");
    }
    if (FindFunctionCall(entryFunction, "tex2Dbias"))
    {
        m_writer.WriteLine(0, "inline float4 tex2Dbias(Texture2DSampler ts, float4 texCoordBias) {");
        m_writer.WriteLine(1, "return ts.t.sample(ts.s, texCoordBias.xy, bias(texCoordBias.w));");
        m_writer.WriteLine(0, "}");
    }
    
    if (FindFunctionCall(entryFunction, "tex3D") ||
        FindFunctionCall(entryFunction, "tex3Dlod"))
    {
        m_writer.WriteLine(0, "struct Texture3DSampler {");
        m_writer.WriteLine(1, "const thread texture3d<float>& t;");
        m_writer.WriteLine(1, "const thread sampler& s;");
        m_writer.WriteLine(1, "Texture3DSampler(thread const texture3d<float>& t, thread const sampler& s) : t(t), s(s) {};");
        m_writer.WriteLine(0, "};");
    }
    if (FindFunctionCall(entryFunction, "tex3D"))
    {
        m_writer.WriteLine(0, "inline float4 tex3D(Texture3DSampler ts, float3 texCoord) {");
        m_writer.WriteLine(1, "return ts.t.sample(ts.s, texCoord);");
        m_writer.WriteLine(0, "}");
    }
    if (FindFunctionCall(entryFunction, "tex3Dlod"))
    {
        m_writer.WriteLine(0, "inline float4 tex3Dlod(Texture3DSampler ts, float4 texCoordMip) {");
        m_writer.WriteLine(1, "return ts.t.sample(ts.s, texCoordMip.xyz, level(texCoordMip.w));");
        m_writer.WriteLine(0, "}");
    }
    
    if (FindFunctionCall(entryFunction, "texCUBElod"))
    {
        m_writer.WriteLine(0, "struct TextureCubeSampler {");
        m_writer.WriteLine(1, "const thread texturecube<float>& t;");
        m_writer.WriteLine(1, "const thread sampler& s;");
        m_writer.WriteLine(1, "TextureCubeSampler(thread const texturecube<float>& t, thread const sampler& s) : t(t), s(s) {};");
        m_writer.WriteLine(0, "};");
        
        m_writer.WriteLine(0, "inline float4 texCUBElod(TextureCubeSampler ts, float4 texCoordMip) {");
        m_writer.WriteLine(1, "return ts.t.sample(ts.s, texCoordMip.xyz, level(texCoordMip.w));");
        m_writer.WriteLine(0, "}");
    }
    
    if (FindFunctionCall(entryFunction, "tex2Dcmp"))
    {
        m_writer.WriteLine(0, "struct Texture2DShadowSampler {");
        m_writer.WriteLine(1, "const thread depth2d<float>& t;");
        m_writer.WriteLine(1, "const thread sampler& s;");
        m_writer.WriteLine(1, "Texture2DShadowSampler(thread const depth2d<float>& t, thread const sampler& s) : t(t), s(s) {};");
        m_writer.WriteLine(0, "};");
        
        m_writer.WriteLine(0, "inline float4 tex2Dcmp(Texture2DShadowSampler ts, float4 texCoordCompare) {");
        if (flags & Flags_ConstShadowSampler) {
            // iOS Metal requires that the sampler in sample_compare is a compile-time constant
            m_writer.WriteLine(1, "constexpr sampler shadow_constant_sampler(mip_filter::none, min_filter::linear, mag_filter::linear, address::clamp_to_edge, compare_func::less);");
            m_writer.WriteLine(1, "return ts.t.sample_compare(shadow_constant_sampler, texCoordCompare.xy, texCoordCompare.z);");
        }
        else
            m_writer.WriteLine(1, "return ts.t.sample_compare(ts.s, texCoordCompare.xy, texCoordCompare.z);");
        m_writer.WriteLine(0, "}");
    }
    
    if (FindFunctionCall(entryFunction, "tex2DMSfetch"))
    {
        m_writer.WriteLine(0, "inline float4 tex2DMSfetch(texture2d_ms<float> t, uint2 texCoord, uint sample) {");
        m_writer.WriteLine(1, "return t.read(texCoord, sample);");
        m_writer.WriteLine(0, "}");

        m_writer.WriteLine(0, "inline float4 tex2DMSfetch(texture2d_ms<float> t, float2 texCoord, uint sample) {");
        m_writer.WriteLine(1, "return t.read(uint2(texCoord), sample);");
        m_writer.WriteLine(0, "}");
    }
    
    const char* shaderClassName = (target == MSLGenerator::Target_VertexShader) ? "Vertex_Shader" : "Pixel_Shader";
    m_writer.WriteLine(0, "struct %s {", shaderClassName);
    
    HLSLRoot* root = m_tree->GetRoot();
    OutputStatements(1, root->statement);

    // Generate constructor
    m_writer.WriteLine(0, "");
    m_writer.BeginLine(1);

    m_writer.Write("%s(", shaderClassName);
    const ClassArgument* currentArg = m_firstClassArgument;
    while (currentArg != NULL)
    {
        if (currentArg->type.addressSpace == HLSLAddressSpace_Constant) m_writer.Write("constant ");
        else m_writer.Write("thread ");

        m_writer.Write("%s & %s", GetTypeName(currentArg->type), currentArg->name);

        currentArg = currentArg->nextArg;
        if (currentArg) m_writer.Write(", ");
    }
    m_writer.Write(")");

    currentArg = m_firstClassArgument;
    if (currentArg) m_writer.Write(" : ");
    while (currentArg != NULL)
    {
        m_writer.Write("%s(%s)", currentArg->name, currentArg->name);
        currentArg = currentArg->nextArg;
        if (currentArg) m_writer.Write(", ");
    }
    m_writer.EndLine(" {}");

    m_writer.WriteLine(0, "};"); // Class
    

    // Generate actual entry point
    m_writer.WriteLine(0, "");

    m_writer.BeginLine(0);

    // @@ Add/Translate function attributes.
    // entryFunction->attributes

    if (m_target == Target_VertexShader) {
        m_writer.Write("vertex ");
    }
    else {
        m_writer.Write("fragment ");
    }

    // Return type.
    if (entryFunction->returnType.baseType == HLSLBaseType_UserDefined) {
        m_writer.Write("%s::", shaderClassName);
    }
    m_writer.Write("%s", GetTypeName(entryFunction->returnType));

    m_writer.Write(" %s(", entryName);

    int argumentCount = 0;
    HLSLArgument * argument = entryFunction->argument;
    while (argument != NULL) {
        if (!argument->hidden) {
            if (argument->type.baseType == HLSLBaseType_UserDefined) {
                m_writer.Write("%s::", shaderClassName);
            }
            m_writer.Write("%s %s", GetTypeName(argument->type), argument->name);

            // @@ IC: We are assuming that the first argument is the 'stage_in'.
            if (argument->type.baseType == HLSLBaseType_UserDefined && argument == entryFunction->argument) {
                m_writer.Write(" [[stage_in]]");
            }
            else if (argument->sv_semantic) {
                m_writer.Write(" [[%s]]", argument->sv_semantic);
            }
            argumentCount++;
        }
        argument = argument->nextArgument;
        if (argument && !argument->hidden) m_writer.Write(", ");
    }

    currentArg = m_firstClassArgument;
    if (argumentCount && currentArg != NULL) m_writer.Write(", ");
    while (currentArg != NULL)
    {
        //if (currentArg->type.addressSpace == HLSLAddressSpace_Constant) m_writer.Write("constant ");
        //else m_writer.Write("thread ");

        if (currentArg->type.baseType == HLSLBaseType_UserDefined) {
            m_writer.Write("constant %s::%s & %s [[%s]]", shaderClassName, currentArg->type.typeName, currentArg->name, currentArg->registerName);
        }
        else {
            m_writer.Write("%s %s [[%s]]", GetTypeName(currentArg->type), currentArg->name, currentArg->registerName);
        }

        currentArg = currentArg->nextArg;
        if (currentArg) m_writer.Write(", ");
    }
    m_writer.EndLine(") {");

    // Create the helper class instance and call real entry point
    m_writer.BeginLine(1);
    m_writer.Write("%s %s", shaderClassName, entryName);

    currentArg = m_firstClassArgument;
    if (currentArg) {
        m_writer.Write("(");

        while (currentArg != NULL)
        {
            m_writer.Write("%s", currentArg->name);
            currentArg = currentArg->nextArg;
            if (currentArg) m_writer.Write(", ");
        }

        m_writer.Write(")");
    }
    m_writer.EndLine(";");

    m_writer.BeginLine(1);
    m_writer.Write("return %s.%s(", entryName, entryName);

    argument = entryFunction->argument;
    while (argument != NULL) {
        if (!argument->hidden) {
            m_writer.Write("%s", argument->name);
        }
        argument = argument->nextArgument;
        if (argument && !argument->hidden) m_writer.Write(", ");
    }

    m_writer.EndLine(");");

    m_writer.WriteLine(0, "}");


    
    CleanPrepass();
    m_tree = NULL;
    
    // Any final check goes here, but shouldn't be needed as the Metal compiler is solid
    
    return !m_error;
}

const char* MSLGenerator::GetResult() const
{
    return m_writer.GetResult();
}

void MSLGenerator::OutputStatements(int indent, HLSLStatement* statement)
{
    // Main generator loop: called recursively
    while (statement != NULL)
    {
        if (statement->hidden) 
        {
            statement = statement->nextStatement;
            continue;
        }

        OutputAttributes(indent, statement->attributes);

        if (statement->nodeType == HLSLNodeType_Declaration)
        {
            HLSLDeclaration* declaration = static_cast<HLSLDeclaration*>(statement);
            m_writer.BeginLine(indent, declaration->fileName, declaration->line);
            OutputDeclaration(declaration);
            m_writer.EndLine(";");
        }
        else if (statement->nodeType == HLSLNodeType_Struct)
        {
            HLSLStruct* structure = static_cast<HLSLStruct*>(statement);
            OutputStruct(indent, structure);
        }
        else if (statement->nodeType == HLSLNodeType_Buffer)
        {
            HLSLBuffer* buffer = static_cast<HLSLBuffer*>(statement);
            OutputBuffer(indent, buffer);
        }
        else if (statement->nodeType == HLSLNodeType_Function)
        {
            HLSLFunction* function = static_cast<HLSLFunction*>(statement);
            OutputFunction(indent, function);
        }
        else if (statement->nodeType == HLSLNodeType_ExpressionStatement)
        {
            HLSLExpressionStatement* expressionStatement = static_cast<HLSLExpressionStatement*>(statement);
            
            // IC: This works, but it only helps in very few scenarios. We need a more general solution that involves more complex syntax tree transformations.
            /*if (expressionStatement->expression->nodeType == HLSLNodeType_FunctionCall)
            {
                OutputFunctionCallStatement(indent, (HLSLFunctionCall *)expressionStatement->expression);
            }
            else*/
            {
                m_writer.BeginLine(indent, statement->fileName, statement->line);
                OutputExpression(expressionStatement->expression, NULL);
                m_writer.EndLine(";");
            }
        }
        else if (statement->nodeType == HLSLNodeType_ReturnStatement)
        {
            HLSLReturnStatement* returnStatement = static_cast<HLSLReturnStatement*>(statement);
            if (returnStatement->expression != NULL)
            {
                m_writer.BeginLine(indent, returnStatement->fileName, returnStatement->line);
                m_writer.Write("return ");
                OutputExpression(returnStatement->expression, NULL);
                m_writer.EndLine(";");
            }
            else
            {
                m_writer.WriteLine(indent, returnStatement->fileName, returnStatement->line, "return;");
            }
        }
        else if (statement->nodeType == HLSLNodeType_DiscardStatement)
        {
            HLSLDiscardStatement* discardStatement = static_cast<HLSLDiscardStatement*>(statement);
            m_writer.WriteLine(indent, discardStatement->fileName, discardStatement->line, "discard_fragment();");
        }
        else if (statement->nodeType == HLSLNodeType_BreakStatement)
        {
            HLSLBreakStatement* breakStatement = static_cast<HLSLBreakStatement*>(statement);
            m_writer.WriteLine(indent, breakStatement->fileName, breakStatement->line, "break;");
        }
        else if (statement->nodeType == HLSLNodeType_ContinueStatement)
        {
            HLSLContinueStatement* continueStatement = static_cast<HLSLContinueStatement*>(statement);
            m_writer.WriteLine(indent, continueStatement->fileName, continueStatement->line, "continue;");
        }
        else if (statement->nodeType == HLSLNodeType_IfStatement)
        {
            HLSLIfStatement* ifStatement = static_cast<HLSLIfStatement*>(statement);
            m_writer.BeginLine(indent, ifStatement->fileName, ifStatement->line);
            m_writer.Write("if (");
            OutputExpression(ifStatement->condition, NULL);
            m_writer.Write(") {");
            m_writer.EndLine();
            OutputStatements(indent + 1, ifStatement->statement);
            m_writer.WriteLine(indent, "}");
            if (ifStatement->elseStatement != NULL)
            {
                m_writer.WriteLine(indent, "else {");
                OutputStatements(indent + 1, ifStatement->elseStatement);
                m_writer.WriteLine(indent, "}");
            }
        }
        else if (statement->nodeType == HLSLNodeType_ForStatement)
        {
            HLSLForStatement* forStatement = static_cast<HLSLForStatement*>(statement);
            m_writer.BeginLine(indent, forStatement->fileName, forStatement->line);
            m_writer.Write("for (");
            OutputDeclaration(forStatement->initialization);
            m_writer.Write("; ");
            OutputExpression(forStatement->condition, NULL);
            m_writer.Write("; ");
            OutputExpression(forStatement->increment, NULL);
            m_writer.Write(") {");
            m_writer.EndLine();
            OutputStatements(indent + 1, forStatement->statement);
            m_writer.WriteLine(indent, "}");
        }
        else if (statement->nodeType == HLSLNodeType_BlockStatement)
        {
            HLSLBlockStatement* blockStatement = static_cast<HLSLBlockStatement*>(statement);
            m_writer.WriteLine(indent, blockStatement->fileName, blockStatement->line, "{");
            OutputStatements(indent + 1, blockStatement->statement);
            m_writer.WriteLine(indent, "}");
        }
        else if (statement->nodeType == HLSLNodeType_Technique)
        {
            // Techniques are ignored.
        }
        else if (statement->nodeType == HLSLNodeType_Pipeline)
        {
            // Pipelines are ignored.
        }
        else
        {
            // Unhandled statement type.
            ASSERT(0);
        }

        statement = statement->nextStatement;
    }
}

// Called by OutputStatements

void MSLGenerator::OutputAttributes(int indent, HLSLAttribute* attribute)
{
    // ACoget-TODO: do those exist in MSL?
    while (attribute != NULL) {
        if (attribute->attributeType == HLSLAttributeType_Unroll) {
            // @@ Do any of these work?
            //m_writer.WriteLine(indent, attribute->fileName, attribute->line, "#pragma unroll");
            //m_writer.WriteLine(indent, attribute->fileName, attribute->line, "[[unroll]]");
        }
        else if (attribute->attributeType == HLSLAttributeType_Flatten) {
            // @@
        }
        else if (attribute->attributeType == HLSLAttributeType_Branch) {
            // @@
        }
        
        attribute = attribute->nextAttribute;
    }
}

void MSLGenerator::OutputDeclaration(HLSLDeclaration* declaration)
{
    if (IsSamplerType(declaration->type))
    {
        // Declare a texture and a sampler instead
        // We do not handle multiple textures on the same line
        ASSERT(declaration->nextDeclaration == NULL);
        if (declaration->type.baseType == HLSLBaseType_Sampler2D)
            m_writer.Write("thread texture2d<float>& %s_texture; thread sampler& %s_sampler", declaration->name, declaration->name);
        else if (declaration->type.baseType == HLSLBaseType_Sampler3D)
            m_writer.Write("thread texture3d<float>& %s_texture; thread sampler& %s_sampler", declaration->name, declaration->name);
        else if (declaration->type.baseType == HLSLBaseType_SamplerCube)
            m_writer.Write("thread texturecube<float>& %s_texture; thread sampler& %s_sampler", declaration->name, declaration->name);
        else if (declaration->type.baseType == HLSLBaseType_Sampler2DShadow)
            m_writer.Write("thread depth2d<float>& %s_texture; thread sampler& %s_sampler", declaration->name, declaration->name);
        else if (declaration->type.baseType == HLSLBaseType_Sampler2DMS)
            m_writer.Write("thread texture2d_ms<float>& %s_texture", declaration->name);
        else
            m_writer.Write("<unhandled texture type>");
    }
    else
    {
        OutputDeclaration(declaration->type, declaration->name, declaration->assignment);
        declaration = declaration->nextDeclaration;
        while(declaration != NULL) {
            m_writer.Write(",");
            OutputDeclarationBody(declaration->type, declaration->name, declaration->assignment);
            declaration = declaration->nextDeclaration;
        };
    }
}

void MSLGenerator::OutputStruct(int indent, HLSLStruct* structure)
{
    m_writer.WriteLine(indent, structure->fileName, structure->line, "struct %s {", structure->name);
    HLSLStructField* field = structure->field;
    while (field != NULL)
    {
        if (!field->hidden)
        {
            m_writer.BeginLine(indent + 1, field->fileName, field->line);

            // ACoget-TODO: no assignment in struct fields?
            OutputDeclaration(field->type, field->name, NULL);

            if (field->sv_semantic) {
                m_writer.Write(" [[%s]]", field->sv_semantic);
            }

            m_writer.Write(";");
            m_writer.EndLine();
        }
        field = field->nextField;
    }
    m_writer.WriteLine(indent, "};");
}

void MSLGenerator::OutputBuffer(int indent, HLSLBuffer* buffer)
{
    HLSLDeclaration* field = buffer->field;
    
    m_writer.BeginLine(indent, buffer->fileName, buffer->line);
    m_writer.Write("struct Uniforms_%s", buffer->name);
    m_writer.EndLine(" {");
    while (field != NULL)
    {
        if (!field->hidden)
        {
            m_writer.BeginLine(indent + 1, field->fileName, field->line);
            OutputDeclaration(field->type, field->name, field->assignment, false, false, /*alignment=*/16);
            m_writer.EndLine(";");
        }
        field = (HLSLDeclaration*)field->nextStatement;
    }
    m_writer.WriteLine(indent, "};");

    // Output member reference to buffers.
    if (String_Equal(buffer->name, "per_pass")) {
        m_per_pass_buffer = buffer;
        m_writer.WriteLine(indent, "constant Uniforms_per_pass & per_pass;");
    }
    else if (String_Equal(buffer->name, "per_item")) {
        m_per_item_buffer = buffer;
        m_writer.WriteLine(indent, "constant Uniforms_per_item & per_item;");
    }
    else {
        ASSERT(0); // General buffers not supported yet.
    }
}
    
void MSLGenerator::OutputFunction(int indent, HLSLFunction* function)
{
    const char* functionName   = function->name;
    const char* returnTypeName = GetTypeName(function->returnType);
    
    m_writer.BeginLine(indent, function->fileName, function->line);
    m_writer.Write("%s %s(", returnTypeName, functionName);
    
    OutputArguments(function->argument);
    
    m_writer.EndLine(") {");
    
    OutputStatements(indent + 1, function->statement);
    m_writer.WriteLine(indent, "};");
}


static bool is_in_buffer(HLSLBuffer * buffer, const char * name)
{
    if (buffer == NULL)
    {
        return false;
    }
    
    HLSLDeclaration* field = buffer->field;
    while (field != NULL)
    {
        if (!field->hidden)
        {
            if (strcmp(field->name, name) == 0) return true;
        }
        field = (HLSLDeclaration*)field->nextStatement;
    }
    return false;
}


// @@ We could be a lot smarter removing parenthesis based on the operator precedence of the parent expression.
static bool needsParenthesis(HLSLExpression * expression, HLSLExpression * parentExpression) {

    // For now we just omit the parenthesis if there's no parent expression.
    if (parentExpression == NULL) return false;

    // One more special case that's pretty common.
    if (parentExpression->nodeType == HLSLNodeType_MemberAccess)
    {
        if (expression->nodeType == HLSLNodeType_IdentifierExpression ||
            expression->nodeType == HLSLNodeType_ArrayAccess ||
            expression->nodeType == HLSLNodeType_MemberAccess)
        {
            return false;
        }
    }

    return true;
}

void MSLGenerator::OutputExpression(HLSLExpression* expression, HLSLExpression* parentExpression)
{
    if (expression->nodeType == HLSLNodeType_IdentifierExpression)
    {
        HLSLIdentifierExpression* identifierExpression = static_cast<HLSLIdentifierExpression*>(expression);
        const char* name = identifierExpression->name;
        // For texture declaration, generate a struct instead
        if (identifierExpression->global && IsSamplerType(identifierExpression->expressionType))
        {
            if (identifierExpression->expressionType.baseType == HLSLBaseType_Sampler2D)
                m_writer.Write("Texture2DSampler(%s_texture, %s_sampler)", name, name);
            else if (identifierExpression->expressionType.baseType == HLSLBaseType_Sampler3D)
                m_writer.Write("Texture3DSampler(%s_texture, %s_sampler)", name, name);
            else if (identifierExpression->expressionType.baseType == HLSLBaseType_SamplerCube)
                m_writer.Write("TextureCubeSampler(%s_texture, %s_sampler)", name, name);
            else if (identifierExpression->expressionType.baseType == HLSLBaseType_Sampler2DShadow)
                m_writer.Write("Texture2DShadowSampler(%s_texture, %s_sampler)", name, name);
            else if (identifierExpression->expressionType.baseType == HLSLBaseType_Sampler2DMS)
                m_writer.Write("%s_texture", name);
            else
                m_writer.Write("<unhandled texture type>");
        }
        else
        {
            if (identifierExpression->global)
            {
                if (is_in_buffer(m_per_item_buffer, name)) m_writer.Write("per_item.");
                else if (is_in_buffer(m_per_pass_buffer, name)) m_writer.Write("per_pass.");
            }
            m_writer.Write("%s", name);

            // IC: Add swizzle if this is a member access of a field that has the swizzle flag.
            /*if (parentExpression->nodeType == HLSLNodeType_MemberAccess)
            {
                HLSLMemberAccess * memberAccess = (HLSLMemberAccess *)parentExpression;
                const HLSLType & objectType = memberAccess->object->expressionType;
                const HLSLStruct* structure = m_tree->FindGlobalStruct(objectType.typeName);
                if (structure != NULL)
                {
                    const HLSLStructField* field = structure->field;
                    while (field != NULL)
                    {
                        if (field->name == name)
                        {
                            if (field->type.flags & HLSLTypeFlag_Swizzle_BGRA)
                            {
                                m_writer.Write(".bgra", name);
                            }
                        }
                    }
                }
            }*/
        }
    }
    else if (expression->nodeType == HLSLNodeType_CastingExpression)
    {
        HLSLCastingExpression* castingExpression = static_cast<HLSLCastingExpression*>(expression);
        m_writer.Write("(");
        OutputDeclarationType(castingExpression->type);
        m_writer.Write(")(");
        OutputExpression(castingExpression->expression, castingExpression);
        m_writer.Write(")");
    }
    else if (expression->nodeType == HLSLNodeType_ConstructorExpression)
    {
        HLSLConstructorExpression* constructorExpression = static_cast<HLSLConstructorExpression*>(expression);
        // ACoget-TODO: should this use OutputDeclarationType?
        m_writer.Write("%s(", GetTypeName(constructorExpression->type));
        OutputExpressionList(constructorExpression->argument);
        m_writer.Write(")");
    }
    else if (expression->nodeType == HLSLNodeType_LiteralExpression)
    {
        HLSLLiteralExpression* literalExpression = static_cast<HLSLLiteralExpression*>(expression);
        switch (literalExpression->type)
        {
            case HLSLBaseType_Half:
            case HLSLBaseType_Float:
            {
                // Don't use printf directly so that we don't use the system locale.
                char buffer[64];
                String_FormatFloat(buffer, sizeof(buffer), literalExpression->fValue);
                m_writer.Write("%s", buffer);
            }
                break;
            case HLSLBaseType_Int:
                m_writer.Write("%d", literalExpression->iValue);
                break;
            case HLSLBaseType_Bool:
                m_writer.Write("%s", literalExpression->bValue ? "true" : "false");
                break;
            default:
                ASSERT(0);
        }
    }
    else if (expression->nodeType == HLSLNodeType_UnaryExpression)
    {
        HLSLUnaryExpression* unaryExpression = static_cast<HLSLUnaryExpression*>(expression);
        const char* op = "?";
        bool pre = true;
        switch (unaryExpression->unaryOp)
        {
            case HLSLUnaryOp_Negative:      op = "-";  break;
            case HLSLUnaryOp_Positive:      op = "+";  break;
            case HLSLUnaryOp_Not:           op = "!";  break;
            case HLSLUnaryOp_BitNot:        op = "~";  break;
            case HLSLUnaryOp_PreIncrement:  op = "++"; break;
            case HLSLUnaryOp_PreDecrement:  op = "--"; break;
            case HLSLUnaryOp_PostIncrement: op = "++"; pre = false; break;
            case HLSLUnaryOp_PostDecrement: op = "--"; pre = false; break;
        }
        bool addParenthesis = needsParenthesis(unaryExpression->expression, expression);
        if (addParenthesis) m_writer.Write("(");
        if (pre)
        {
            m_writer.Write("%s", op);
            OutputExpression(unaryExpression->expression, unaryExpression);
        }
        else
        {
            OutputExpression(unaryExpression->expression, unaryExpression);
            m_writer.Write("%s", op);
        }
        if (addParenthesis) m_writer.Write(")");
    }
    else if (expression->nodeType == HLSLNodeType_BinaryExpression)
    {
        HLSLBinaryExpression* binaryExpression = static_cast<HLSLBinaryExpression*>(expression);

        bool addParenthesis = needsParenthesis(expression, parentExpression);
        if (addParenthesis) m_writer.Write("(");

        bool rewrite_assign = false;
        if (binaryExpression->binaryOp == HLSLBinaryOp_Assign && binaryExpression->expression1->nodeType == HLSLNodeType_ArrayAccess) {
            HLSLArrayAccess* arrayAccess = static_cast<HLSLArrayAccess*>(binaryExpression->expression1);
            if (!arrayAccess->array->expressionType.array && IsMatrixType(arrayAccess->array->expressionType.baseType)) {
                rewrite_assign = true;

                m_writer.Write("set_column(");
                OutputExpression(arrayAccess->array, NULL);
                m_writer.Write(", ");
                OutputExpression(arrayAccess->index, NULL);
                m_writer.Write(", ");
                OutputExpression(binaryExpression->expression2, NULL);
                m_writer.Write(")");
            }
        }

        if (!rewrite_assign)
        {
            OutputExpression(binaryExpression->expression1, binaryExpression);
            const char* op = "?";
            switch (binaryExpression->binaryOp)
            {
                case HLSLBinaryOp_Add:          op = " + "; break;
                case HLSLBinaryOp_Sub:          op = " - "; break;
                case HLSLBinaryOp_Mul:          op = " * "; break;
                case HLSLBinaryOp_Div:          op = " / "; break;
                case HLSLBinaryOp_Less:         op = " < "; break;
                case HLSLBinaryOp_Greater:      op = " > "; break;
                case HLSLBinaryOp_LessEqual:    op = " <= "; break;
                case HLSLBinaryOp_GreaterEqual: op = " >= "; break;
                case HLSLBinaryOp_Equal:        op = " == "; break;
                case HLSLBinaryOp_NotEqual:     op = " != "; break;
                case HLSLBinaryOp_Assign:       op = " = "; break;
                case HLSLBinaryOp_AddAssign:    op = " += "; break;
                case HLSLBinaryOp_SubAssign:    op = " -= "; break;
                case HLSLBinaryOp_MulAssign:    op = " *= "; break;
                case HLSLBinaryOp_DivAssign:    op = " /= "; break;
                case HLSLBinaryOp_And:          op = " && "; break;
                case HLSLBinaryOp_Or:           op = " || "; break;
                case HLSLBinaryOp_BitAnd:       op = " & "; break;
                case HLSLBinaryOp_BitOr:        op = " | "; break;
                case HLSLBinaryOp_BitXor:       op = " ^ "; break;
                default:
                    ASSERT(0);
            }
            m_writer.Write("%s", op);
            OutputExpression(binaryExpression->expression2, binaryExpression);
        }
        if (addParenthesis) m_writer.Write(")");
    }
    else if (expression->nodeType == HLSLNodeType_ConditionalExpression)
    {
        HLSLConditionalExpression* conditionalExpression = static_cast<HLSLConditionalExpression*>(expression);
        // @@ Remove parenthesis.
        m_writer.Write("((");
        OutputExpression(conditionalExpression->condition, NULL);
        m_writer.Write(")?(");
        OutputExpression(conditionalExpression->trueExpression, NULL);
        m_writer.Write("):(");
        OutputExpression(conditionalExpression->falseExpression, NULL);
        m_writer.Write("))");
    }
    else if (expression->nodeType == HLSLNodeType_MemberAccess)
    {
        HLSLMemberAccess* memberAccess = static_cast<HLSLMemberAccess*>(expression);
        bool addParenthesis = needsParenthesis(memberAccess->object, expression);
        
        if (addParenthesis) m_writer.Write("(");
        OutputExpression(memberAccess->object, NULL);
        if (addParenthesis) m_writer.Write(")");
        
        m_writer.Write(".%s", memberAccess->field);
    }
    else if (expression->nodeType == HLSLNodeType_ArrayAccess)
    {
        HLSLArrayAccess* arrayAccess = static_cast<HLSLArrayAccess*>(expression);
        if (arrayAccess->array->expressionType.array || !IsMatrixType(arrayAccess->array->expressionType.baseType))
        {
            OutputExpression(arrayAccess->array, expression);
            m_writer.Write("[");
            OutputExpression(arrayAccess->index, NULL);
            m_writer.Write("]");
        }
        else
        {
            // @@ This doesn't work for l-values!
            m_writer.Write("column(");
            OutputExpression(arrayAccess->array, NULL);
            m_writer.Write(", ");
            OutputExpression(arrayAccess->index, NULL);
            m_writer.Write(")");
        }
    }
    else if (expression->nodeType == HLSLNodeType_FunctionCall)
    {
        HLSLFunctionCall* functionCall = static_cast<HLSLFunctionCall*>(expression);
        OutputFunctionCall(functionCall);
    }
    else
    {
        m_writer.Write("<unknown expression>");
    }
}
 
// Called by the various Output functions
void MSLGenerator::OutputArguments(HLSLArgument* argument)
{
    int numArgs = 0;
    while (argument != NULL)
    {
        if (argument->hidden) {
            argument = argument->nextArgument;
            continue;
        }

        if (numArgs > 0)
        {
            m_writer.Write(", ");
        }
        
        bool isRef = false;
        bool isConst = false;
        if (argument->modifier == HLSLArgumentModifier_Out || argument->modifier == HLSLArgumentModifier_Inout)
        {
            isRef = true;
            
        }
        if (argument->modifier == HLSLArgumentModifier_In || argument->modifier == HLSLArgumentModifier_Const)
        {
            isConst = true;
        }

        OutputDeclaration(argument->type, argument->name, argument->defaultValue, isRef, isConst);
        argument = argument->nextArgument;
        ++numArgs;
    }
}
    
void MSLGenerator::OutputDeclaration(const HLSLType& type, const char* name, HLSLExpression* assignment, bool isRef, bool isConst, int alignment)
{
    OutputDeclarationType(type, isRef, isConst, alignment);
    OutputDeclarationBody(type, name, assignment, isRef);
}

void MSLGenerator::OutputDeclarationType(const HLSLType& type, bool isRef, bool isConst, int alignment)
{
    const char* typeName = GetTypeName(type);
    if (isRef)
    {
        m_writer.Write("thread ");
    }
    if (isConst || type.flags & HLSLTypeFlag_Const)
    {
        m_writer.Write("const ");

        if (type.flags & HLSLTypeFlag_Static)
        {
            m_writer.Write("static constant constexpr ");
        }
    }
    if (IsSamplerType(type))
    {
        if (type.baseType == HLSLBaseType_Sampler2D)
            typeName = "Texture2DSampler";
        else if (type.baseType == HLSLBaseType_Sampler3D)
            typeName = "Texture3DSampler";
        else if (type.baseType == HLSLBaseType_SamplerCube)
            typeName = "TextureCubeSampler";
        else if (type.baseType == HLSLBaseType_Sampler2DShadow)
            typeName = "Texture2DShadowSampler";
        else if (type.baseType == HLSLBaseType_Sampler2DShadow)
            typeName = "Texture2DMSSampler";
        else
            typeName = "<unhandled texture type>";
    }
    else if (alignment != 0)
    {
        m_writer.Write("alignas(%d) ", alignment);
    }

    m_writer.Write("%s", typeName);

    // Interpolation modifiers.
    if (type.flags & HLSLTypeFlag_NoInterpolation)
    {
        m_writer.Write(" [[flat]]");
    }
    else
    {
        if (type.flags & HLSLTypeFlag_NoPerspective)
        {
            if (type.flags & HLSLTypeFlag_Centroid)
            {
                m_writer.Write(" [[centroid_no_perspective]]");
            }
            else if (type.flags & HLSLTypeFlag_Sample)
            {
                m_writer.Write(" [[sample_no_perspective]]");
            }
            else
            {
                m_writer.Write(" [[center_no_perspective]]");
            }
        }
        else
        {
            if (type.flags & HLSLTypeFlag_Centroid)
            {
                m_writer.Write(" [[centroid_perspective]]");
            }
            else if (type.flags & HLSLTypeFlag_Sample)
            {
                m_writer.Write(" [[sample_perspective]]");
            }
            else
            {
                // Default.
                //m_writer.Write(" [[center_perspective]]");
            }
        }
    }
}
    
void MSLGenerator::OutputDeclarationBody(const HLSLType& type, const char* name, HLSLExpression* assignment, bool isRef)
{
    if (isRef)
    {
        // Arrays of refs are illegal in C++ and hence MSL, need to "link" the & to the var name
        m_writer.Write("(&");
    }
    
    // Then name
    m_writer.Write(" %s", name);
    
    if (isRef)
    {
        m_writer.Write(")");
    }
    
    // Add brackets for arrays
    if (type.array)
    {
        m_writer.Write("[");
        if (type.arraySize != NULL)
        {
            OutputExpression(type.arraySize, NULL);
        }
        m_writer.Write("]");
    }
    
    // Semantics and registers unhandled for now
    
    // Assignment handling
    if (assignment != NULL)
    {
        m_writer.Write(" = ");
        if (type.array)
        {
            m_writer.Write("{ ");
            OutputExpressionList(assignment);
            m_writer.Write(" }");
        }
        else
        {
            OutputExpression(assignment, NULL);
        }
    }
}
    
void MSLGenerator::OutputExpressionList(HLSLExpression* expression)
{
    int numExpressions = 0;
    while (expression != NULL)
    {
        if (numExpressions > 0)
        {
            m_writer.Write(", ");
        }
        OutputExpression(expression, NULL);
        expression = expression->nextExpression;
        ++numExpressions;
    }
}

inline bool isAddressable(HLSLExpression * expression)
{
    if (expression->nodeType == HLSLNodeType_IdentifierExpression) return true;
    if (expression->nodeType == HLSLNodeType_ArrayAccess) return true;
    if (expression->nodeType == HLSLNodeType_MemberAccess)
    {
        HLSLMemberAccess * memberAccess = (HLSLMemberAccess *)expression;
        return !memberAccess->swizzle;
    }
    return false;
}

void MSLGenerator::OutputFunctionCallStatement(int indent, HLSLFunctionCall* functionCall)
{
    int argumentIndex = 0;
    HLSLArgument * argument = functionCall->function->argument;
    HLSLExpression * expression = functionCall->argument;
    while (argument != NULL)
    {
        if (!isAddressable(expression))
        {
            if (argument->modifier == HLSLArgumentModifier_Out)
            {
                m_writer.BeginLine(indent, functionCall->fileName, functionCall->line);
                OutputDeclarationType(argument->type);
                m_writer.Write("tmp%d;", argumentIndex);
                m_writer.EndLine();
            }
            else if (argument->modifier == HLSLArgumentModifier_Inout)
            {
                m_writer.BeginLine(indent, functionCall->fileName, functionCall->line);
                OutputDeclarationType(argument->type);
                m_writer.Write("tmp%d = ", argumentIndex);
                OutputExpression(expression, NULL);
                m_writer.EndLine(";");
            }
        }
        argument = argument->nextArgument;
        expression = expression->nextExpression;
        argumentIndex++;
    }

    m_writer.BeginLine(indent, functionCall->fileName, functionCall->line);
    const char* name = functionCall->function->name;
    m_writer.Write("%s(", name);
    //OutputExpressionList(functionCall->argument);

    // Output expression list with temporary substitution.
    argumentIndex = 0;
    argument = functionCall->function->argument;
    expression = functionCall->argument;
    while (expression != NULL)
    {
        if (!isAddressable(expression) && (argument->modifier == HLSLArgumentModifier_Out || argument->modifier == HLSLArgumentModifier_Inout))
        {
            m_writer.Write("tmp%d", argumentIndex);
        }
        else
        {
            OutputExpression(expression, NULL);
        }

        argument = argument->nextArgument;
        expression = expression->nextExpression;
        argumentIndex++;
        if (expression) m_writer.Write(", ");
    }
    m_writer.EndLine(");");

    argumentIndex = 0;
    argument = functionCall->function->argument;
    expression = functionCall->argument;
    while (expression != NULL)
    {
        if (!isAddressable(expression) && (argument->modifier == HLSLArgumentModifier_Out || argument->modifier == HLSLArgumentModifier_Inout))
        {
            m_writer.BeginLine(indent, functionCall->fileName, functionCall->line);
            OutputExpression(expression, NULL);
            m_writer.Write(" = tmp%d", argumentIndex);
            m_writer.EndLine(";");
        }

        argument = argument->nextArgument;
        expression = expression->nextExpression;
        argumentIndex++;
    }
}

void MSLGenerator::OutputFunctionCall(HLSLFunctionCall* functionCall)
{
    // @@ All these shenanigans only work if the function call is a statement...

    const char* name = functionCall->function->name;
    m_writer.Write("%s(", name);
    OutputExpressionList(functionCall->argument);
    m_writer.Write(")");
}

} // M4
