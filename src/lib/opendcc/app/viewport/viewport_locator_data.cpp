// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/app/viewport/viewport_locator_data.h"
#include <pxr/imaging/pxOsd/tokens.h>
#include <pxr/usd/usd/primRange.h>

#include <pxr/usd/sdr/registry.h>
#include <pxr/usd/sdf/assetPath.h>

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

#if PXR_VERSION >= 2002
extern const std::string usd_locator_unlit_source =
    R"#(-- glslfx version 0.1
-- configuration
{
    "techniques": {
        "default": {
            "surfaceShader": {
                "source": [ "unlit" ]
            }
        }
    }
} 
--- --------------------------------------------------------------------------
-- glsl unlit

vec4 surfaceShader(vec4 Peye, vec3 Neye, vec4 color, vec4 patchCoord)
{
    return vec4(ApplyColorOverrides(color.rgbb).rgb, 1);
}
)#";

extern const std::string usd_locator_domelight_source =
    R"#(-- glslfx version 0.1
-- configuration
{
    "textures": {
        "texture" : {
            "documentation" : "domelight texture"
        }
     },
    "techniques": {
        "default": {
            "surfaceShader": {
                "source": [ "domelight" ]
            }
        }
    }
} 
--- --------------------------------------------------------------------------
-- glsl domelight

#define M_PI 3.1415926535897932384626433832795
	vec2 latlong(vec3 dir)
	{
		float theta = acos(dir.y);
		float phi = atan(dir.x, dir.z) + M_PI;
	
		return vec2(1 - phi / (2 * M_PI), 1 - theta / M_PI);
	}

	vec4 surfaceShader(vec4 Peye, vec3 Neye, vec4 color, vec4 patchCoord)
	{
	#ifdef HD_HAS_texture
		vec3 n = normalize(HdGet_points().xyz);
		vec2 uv = latlong(n);
		color = vec4(HdGet_texture(uv).xyz, 1);
	#else
		color = vec4(ApplyColorOverrides(color).rgb, 1);
	#endif
   
		return color;
	}
)#";
#else
extern const std::string usd_locator_unlit_source =
    R"#(vec4 surfaceShader(vec4 Peye, vec3 Neye, vec4 color, vec4 patchCoord)
{
    return vec4(ApplyColorOverrides(color.rgbb).rgb, 1);
}
)#";

extern const std::string usd_locator_domelight_source =
    R"#(#define M_PI 3.1415926535897932384626433832795
	vec2 latlong(vec3 dir)
	{
		float theta = acos(dir.y);
		float phi = atan(dir.x, dir.z) + M_PI;
	
		return vec2(1 - phi / (2 * M_PI), theta / M_PI);
	}

	vec4 surfaceShader(vec4 Peye, vec3 Neye, vec4 color, vec4 patchCoord)
	{
	#ifdef HD_HAS_texture
		vec3 n = normalize(HdGet_points().xyz);
		vec2 uv = latlong(n);
		color = vec4(HdGet_texture(uv).xyz, 1);
	#else
		color = vec4(ApplyColorOverrides(color).rgb, 1);
	#endif
   
		return color;
	}
)#";
#endif

const TfToken& LocatorRenderData::topology() const
{
    static const TfToken none;
    return none;
}

#if PXR_VERSION >= 2002
HdMaterialNetworkMap ViewportLocatorData::get_material_resource(const SdfPath& material_path) const
{
    const auto rprim_path = material_path.GetAbsoluteRootOrPrimPath();
    if (m_materials.find(rprim_path) == m_materials.end())
        return HdMaterialNetworkMap();

    static std::once_flag once;
    static TfToken unlit_source_id;
    static TfToken domelight_source_id;
    std::call_once(once, [] {
        const auto unlit_node = SdrRegistry::GetInstance().GetShaderNodeFromSourceCode(usd_locator_unlit_source, TfToken("glslfx"), {});
        const auto domelight_node = SdrRegistry::GetInstance().GetShaderNodeFromSourceCode(usd_locator_domelight_source, TfToken("glslfx"), {});
        unlit_source_id = unlit_node->GetIdentifier();
        domelight_source_id = domelight_node->GetIdentifier();
    });

    HdMaterialNetworkMap material_network_map;
    material_network_map.terminals.push_back(rprim_path);

    HdMaterialNetwork& material_network = material_network_map.map[HdMaterialTerminalTokens->surface];
    auto texture_iter = m_textures.find(rprim_path);
    if (texture_iter != m_textures.end() && !texture_iter->second.empty())
    {
        const auto texture_path = rprim_path.AppendProperty(TfToken("texture"));
        HdMaterialNode domelight_shader_node = { rprim_path, domelight_source_id };
        domelight_shader_node.parameters[TfToken("texture")] = VtValue(GfVec3f(1, 1, 1));
        HdMaterialNode texture_sampler = { texture_path, SdrRegistry::GetInstance().GetShaderNodeByIdentifier(TfToken("UsdUVTexture"))->GetIdentifier() };
        texture_sampler.parameters[TfToken("file")] = VtValue(SdfAssetPath(texture_iter->second));
        HdMaterialRelationship rel;
        rel.inputId = texture_path;
        rel.inputName = TfToken("rgb");
        rel.outputId = rprim_path;
        rel.outputName = TfToken("texture");

        material_network.nodes = { texture_sampler, domelight_shader_node };
        material_network.relationships = { rel };
    }
    else
    {
        HdMaterialNode unlit_shader_node = { rprim_path, unlit_source_id };
        material_network.nodes.push_back(unlit_shader_node);
    }
    return material_network_map;
}
#else
std::string ViewportLocatorData::get_surface_shader_source(const SdfPath& prim_path) const
{
    if (m_textures.find(prim_path.GetAbsoluteRootOrPrimPath()) != m_textures.end())
    {
        return usd_locator_domelight_source;
    }

    return usd_locator_unlit_source;
}

HdMaterialParamVector ViewportLocatorData::get_material_params(const SdfPath& material_path) const
{
    auto texture_iter = m_textures.find(material_path.GetAbsoluteRootOrPrimPath());
    if (texture_iter == m_textures.end() || texture_iter->second.empty())
    {
        return HdMaterialParamVector();
    }

    HdMaterialParamVector params;
    params.push_back(HdMaterialParam(HdMaterialParam::ParamTypeTexture, TfToken("texture"), VtValue(GfVec4f(0, 0, 0, 1)),
                                     texture_iter->first.AppendProperty(HdPrimTypeTokens->texture)));
    return params;
}

#endif

std::string ViewportLocatorData::get_texture_path(const SdfPath& texture_path) const
{
    auto texture_iter = m_textures.find(texture_path.GetAbsoluteRootOrPrimPath());
    return texture_iter == m_textures.end() ? "" : texture_iter->second;
}

bool ViewportLocatorData::contains_material(const SdfPath& path) const
{
    return m_materials.find(path) != m_materials.end();
}

bool ViewportLocatorData::contains_texture(const SdfPath& path) const
{
    auto texture_iter = m_textures.find(path);
    return texture_iter != m_textures.end() && !texture_iter->second.empty();
}

bool ViewportLocatorData::contains_light(const SdfPath& path) const
{
    return m_lights.find(path) != m_lights.end();
}

OPENDCC_NAMESPACE_CLOSE