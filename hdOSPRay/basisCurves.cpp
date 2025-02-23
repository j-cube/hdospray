// Copyright 2019 Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include "basisCurves.h"
#include "config.h"
#include "context.h"
#include "instancer.h"
#include "material.h"
#include "renderParam.h"
#include "renderPass.h"

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/imaging/hd/meshUtil.h>
#include <pxr/imaging/hd/smoothNormals.h>
#include <pxr/imaging/pxOsd/tokens.h>

#include <rkcommon/math/AffineSpace.h>

using namespace rkcommon::math;

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(
    HdOSPRayTokens,
    (st)
);
// clang-format on

HdOSPRayBasisCurves::HdOSPRayBasisCurves(SdfPath const& id,
                                         SdfPath const& instancerId)
#if HD_API_VERSION < 36
    : HdBasisCurves(id, instancerId)
#else
    : HdBasisCurves(id)
#endif
{
}

HdDirtyBits
HdOSPRayBasisCurves::GetInitialDirtyBitsMask() const
{
    HdDirtyBits mask = HdChangeTracker::Clean | HdChangeTracker::InitRepr
           | HdChangeTracker::DirtyExtent | HdChangeTracker::DirtyNormals
           | HdChangeTracker::DirtyPoints | HdChangeTracker::DirtyPrimID
           | HdChangeTracker::DirtyPrimvar | HdChangeTracker::DirtyDisplayStyle
           | HdChangeTracker::DirtyRepr | HdChangeTracker::DirtyMaterialId
           | HdChangeTracker::DirtyTopology | HdChangeTracker::DirtyTransform
           | HdChangeTracker::DirtyVisibility | HdChangeTracker::DirtyWidths
           | HdChangeTracker::DirtyComputationPrimvarDesc;

    if (!GetInstancerId().IsEmpty()) {
        mask |= HdChangeTracker::DirtyInstancer;
    }

    return (HdDirtyBits)mask;
}

void
HdOSPRayBasisCurves::_InitRepr(TfToken const& reprToken, HdDirtyBits* dirtyBits)
{
    TF_UNUSED(reprToken);
    TF_UNUSED(dirtyBits);
}

HdDirtyBits
HdOSPRayBasisCurves::_PropagateDirtyBits(HdDirtyBits bits) const
{
    return bits;
}

void
HdOSPRayBasisCurves::Sync(HdSceneDelegate* delegate, HdRenderParam* renderParam,
                          HdDirtyBits* dirtyBits, TfToken const& reprToken)
{

    HdOSPRayRenderParam* ospRenderParam
           = static_cast<HdOSPRayRenderParam*>(renderParam);
    opp::Renderer renderer = ospRenderParam->GetOSPRayRenderer();

    SdfPath const& id = GetId();
    bool updateGeometry = false;
    bool isTransformDirty = false;
    if (*dirtyBits & HdChangeTracker::DirtyTopology) {
        _topology = delegate->GetBasisCurvesTopology(id);
        if (_topology.HasIndices()) {
            _indices = _topology.GetCurveIndices();
        }
        updateGeometry = true;
    }
    if (*dirtyBits & HdChangeTracker::DirtyMaterialId) {
    }
    if (*dirtyBits & HdChangeTracker::DirtyPrimvar) {
    }
    if (*dirtyBits & HdChangeTracker::DirtyTransform) {
        _xfm = GfMatrix4f(delegate->GetTransform(id));
        isTransformDirty = true;
    }
    if (*dirtyBits & HdChangeTracker::DirtyVisibility) {
    }
    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->normals)
        || HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->widths)
        || HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points)
        || HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->primvar)
        || HdChangeTracker::IsPrimvarDirty(*dirtyBits, id,
                                           HdTokens->displayColor)
        || HdChangeTracker::IsPrimvarDirty(*dirtyBits, id,
                                           HdTokens->displayOpacity)
        || HdChangeTracker::IsPrimvarDirty(*dirtyBits, id,
                                           HdOSPRayTokens->st)) {
        _UpdatePrimvarSources(delegate, *dirtyBits);
        updateGeometry = true;
    }

    if (updateGeometry) {
        _UpdateOSPRayRepr(delegate, reprToken, dirtyBits, ospRenderParam);
    }

#if HD_API_VERSION < 36
#else
    _UpdateInstancer(delegate, dirtyBits);

    HdInstancer::_SyncInstancerAndParents(delegate->GetRenderIndex(),
                                          GetInstancerId());
#endif

    if ((HdChangeTracker::IsInstancerDirty(*dirtyBits, id) || isTransformDirty)
        && !_geometricModels.empty()) {
        _ospInstances.clear();
        if (!GetInstancerId().IsEmpty()) {
            // Retrieve instance transforms from the instancer.
            HdRenderIndex& renderIndex = delegate->GetRenderIndex();
            HdInstancer* instancer = renderIndex.GetInstancer(GetInstancerId());
            VtMatrix4dArray transforms
                   = static_cast<HdOSPRayInstancer*>(instancer)
                            ->ComputeInstanceTransforms(GetId());

            size_t newSize = transforms.size();

            opp::Group group;
            group.setParam("geometry", opp::CopiedData(_geometricModels));
            group.commit();

            _ospInstances.reserve(newSize);
            for (size_t i = 0; i < newSize; i++) {
                opp::Instance instance(group);

                GfMatrix4f matf = _xfm * GfMatrix4f(transforms[i]);
                float* xfmf = matf.GetArray();
                affine3f xfm(vec3f(xfmf[0], xfmf[1], xfmf[2]),
                             vec3f(xfmf[4], xfmf[5], xfmf[6]),
                             vec3f(xfmf[8], xfmf[9], xfmf[10]),
                             vec3f(xfmf[12], xfmf[13], xfmf[14]));
                instance.setParam("xfm", xfm);
                instance.commit();
                _ospInstances.push_back(instance);
            }
        } else {
            opp::Group group;
            group.setParam("geometry", opp::CopiedData(_geometricModels));
            group.commit();
            opp::Instance instance(group);
            GfMatrix4f matf = _xfm;
            float* xfmf = matf.GetArray();
            affine3f xfm(vec3f(xfmf[0], xfmf[1], xfmf[2]),
                         vec3f(xfmf[4], xfmf[5], xfmf[6]),
                         vec3f(xfmf[8], xfmf[9], xfmf[10]),
                         vec3f(xfmf[12], xfmf[13], xfmf[14]));
            instance.setParam("xfm", xfm);
            instance.commit();
            _ospInstances.push_back(instance);
        }

        ospRenderParam->UpdateModelVersion();
    }

    *dirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;
}

void
HdOSPRayBasisCurves::_UpdatePrimvarSources(HdSceneDelegate* sceneDelegate,
                                           HdDirtyBits dirtyBits)
{
    HD_TRACE_FUNCTION();
    SdfPath const& id = GetId();

    HdPrimvarDescriptorVector primvars;
    for (size_t i = 0; i < HdInterpolationCount; ++i) {
        HdInterpolation interp = static_cast<HdInterpolation>(i);
        primvars = GetPrimvarDescriptors(sceneDelegate, interp);
        for (HdPrimvarDescriptor const& pv : primvars) {
            const auto value = sceneDelegate->Get(id, pv.name);
            if (pv.name == HdTokens->points) {
                if ((dirtyBits & HdChangeTracker::DirtyPoints)
                    && value.IsHolding<VtVec3fArray>()) {
                    _points = value.Get<VtVec3fArray>();
                }
                continue;
            }
            if (pv.name == HdTokens->normals) {
                if ((dirtyBits & HdChangeTracker::DirtyPoints)
                    && value.IsHolding<VtVec3fArray>()) {
                    _normals = value.Get<VtVec3fArray>();
                }
                continue;
            }

            if (pv.name == HdTokens->widths) {
                if (dirtyBits & HdChangeTracker::DirtyWidths) {
                    if (value.IsHolding<VtFloatArray>())
                        _widths = value.Get<VtFloatArray>();
                }
                continue;
            }

            if ((pv.role == HdPrimvarRoleTokens->textureCoordinate
                 || pv.name == HdOSPRayTokens->st)
                && HdChangeTracker::IsPrimvarDirty(dirtyBits, id,
                                                   HdOSPRayTokens->st)) {
                if (value.IsHolding<VtVec2fArray>()) {
                    _texcoords = value.Get<VtVec2fArray>();
                }
                continue;
            }

            if (pv.name == HdTokens->displayColor
                && HdChangeTracker::IsPrimvarDirty(dirtyBits, id,
                                                   HdTokens->displayColor)) {
                if (value.IsHolding<VtVec3fArray>()) {
                    const VtVec3fArray& colors = value.Get<VtVec3fArray>();
                    _colors.resize(colors.size());
                    for (size_t i = 0; i < colors.size(); i++) {
                        _colors[i] = { colors[i].data()[0], colors[i].data()[1],
                                       colors[i].data()[2], 1.f };
                    }
                    if (!_colors.empty()) {
                        if (_colors.size() > 1) {
                            _singleColor = { 1.f, 1.f, 1.f, 1.f };
                        } else {
                            _singleColor = { _colors[0][0], _colors[0][1],
                                             _colors[0][2], 1.f };
                        }
                    }
                }
                continue;
            }

            if (pv.name == HdTokens->displayOpacity
                && HdChangeTracker::IsPrimvarDirty(dirtyBits, id,
                                                   HdTokens->displayOpacity)) {
                if (value.IsHolding<VtFloatArray>()) {
                    const VtFloatArray& opacities = value.Get<VtFloatArray>();
                    if (_colors.size() < opacities.size())
                        _colors.resize(opacities.size());
                    for (size_t i = 0; i < opacities.size(); i++)
                        _colors[i].data()[3] = opacities[i];
                    if (!_colors.empty())
                        _singleColor[3] = _colors[0][3];
                }
                continue;
            }
        }
    }
}

void
HdOSPRayBasisCurves::_UpdateOSPRayRepr(HdSceneDelegate* sceneDelegate,
                                       TfToken const& reprToken,
                                       HdDirtyBits* dirtyBitsState,
                                       HdOSPRayRenderParam* renderParam)
{
    bool hasWidths = (_widths.size() == _points.size());
    bool hasNormals = (_normals.size() == _points.size());
    if (_points.empty()) {
        TF_RUNTIME_ERROR("_UpdateOSPRayRepr: points empty");
        return;
    }

    _ospCurves = opp::Geometry("curve");
    _position_radii.clear();

    for (size_t i = 0; i < _points.size(); i++) {
        const float width = hasWidths ? _widths[i] / 2.f : 1.0f;
        _position_radii.emplace_back(
               vec4f({ _points[i][0], _points[i][1], _points[i][2], width }));
    }

    opp::SharedData vertices = opp::SharedData(
           _position_radii.data(), OSP_VEC4F, _position_radii.size());
    vertices.commit();
    _ospCurves.setParam("vertex.position_radius", vertices);

    opp::SharedData normals;
    if (hasNormals) {
        normals = opp::SharedData(_normals.data(), OSP_VEC3F, _normals.size());
        normals.commit();
        _ospCurves.setParam("vertex.normal", normals);
    }

    auto type = _topology.GetCurveType();
    if (type != HdTokens->cubic) // TODO: linear
        TF_RUNTIME_ERROR("hdosp::basisCurves - Curve type not supported");
    auto basis = _topology.GetCurveBasis();
    const bool hasIndices = !_indices.empty();
    auto vertexCounts = _topology.GetCurveVertexCounts();
    size_t index = 0;
    for (auto vci : vertexCounts) {
        std::vector<unsigned int> indices;
        for (size_t i = 0; i < vci - size_t(3); i++) {
            if (hasIndices)
                indices.push_back(_indices[index]);
            else
                indices.push_back(index);
            index++;
        }
        index += 3;
        auto geometry = opp::Geometry("curve");
        geometry.setParam("vertex.position_radius", vertices);
        if (_colors.size() > 1) {
            opp::SharedData colors = opp::SharedData(_colors.cdata(), OSP_VEC4F,
                                                     _colors.size());
            colors.commit();
            geometry.setParam("vertex.color", colors);
        }

        if (_texcoords.size() > 1) {
            opp::SharedData texcoords = opp::SharedData(
                   _texcoords.cdata(), OSP_VEC2F, _texcoords.size());
            texcoords.commit();
            geometry.setParam("vertex.texcoord", texcoords);
        }

        if (hasNormals)
            geometry.setParam("vertex.normal", normals);

        geometry.setParam("index", opp::CopiedData(indices));
        geometry.setParam("type", OSP_ROUND);
        if (hasNormals)
            geometry.setParam("type", OSP_RIBBON);
        if (basis == HdTokens->bSpline)
            geometry.setParam("basis", OSP_BSPLINE);
        else if (basis == HdTokens->catmullRom)
            geometry.setParam("basis", OSP_CATMULL_ROM);
        else
            TF_RUNTIME_ERROR("hdospBS::sync: unsupported curve basis");
        geometry.commit();
        const HdRenderIndex& renderIndex = sceneDelegate->GetRenderIndex();
        const HdOSPRayMaterial* material
               = static_cast<const HdOSPRayMaterial*>(renderIndex.GetSprim(
                      HdPrimTypeTokens->material, GetMaterialId()));
        opp::Material ospMaterial;
        if (material && material->GetOSPRayMaterial()) {
            ospMaterial = material->GetOSPRayMaterial();
        } else {
            // no material, create a new one
            ospMaterial = HdOSPRayMaterial::CreateDefaultMaterial(_singleColor);
        }

        // Create OSPRay model
        auto gm = opp::GeometricModel(geometry);

        gm.setParam("material", ospMaterial);
        gm.commit();
        _geometricModels.push_back(gm);
    }

    renderParam->UpdateModelVersion();

    if (!_populated) {
        renderParam->AddHdOSPRayBasisCurves(this);
        _populated = true;
    }
}

void
HdOSPRayBasisCurves::AddOSPInstances(
       std::vector<opp::Instance>& instanceList) const
{
    if (IsVisible()) {
        instanceList.insert(instanceList.end(), _ospInstances.begin(),
                            _ospInstances.end());
    }
}