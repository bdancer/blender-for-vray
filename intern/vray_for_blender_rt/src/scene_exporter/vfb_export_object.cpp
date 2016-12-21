/*
 * Copyright (c) 2015, Chaos Software Ltd
 *
 * V-Ray For Blender
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "vfb_node_exporter.h"
#include "vfb_utils_blender.h"
#include "vfb_utils_object.h"
#include "vfb_utils_nodes.h"

uint32_t to_int_layer(const BlLayers & layers) {
	uint32_t res = 0;
	for (int c = 0; c < 20; ++c) {
		res |= layers[c];
	}
	return res;
}

uint32_t get_layer(BL::Object ob, bool use_local, uint32_t scene_layers) {
	const bool is_light = (ob.data() && ob.data().is_a(&RNA_Lamp));
	uint32_t layer = 0;

	auto ob_layers = ob.layers();
	auto ob_local_layers = ob.layers_local_view();

	for (int c = 0; c < 20; c++) {
		if (ob_layers[c]) {
			layer |= (1 << c);
		}
	}

	if (is_light) {
		/* Consider light is visible if it was visible without layer
		 * override, which matches behavior of Blender Internal.
		 */
		if (layer & scene_layers) {
			for (int c = 0; c < 8; c++) {
				layer |= (1 << (20 + c));
			}
		}
	} else {
		for (int c = 0; c < 8; c++) {
			if (ob_local_layers[c]) {
				layer |= (1 << (20 + c));
			}
		}
	}

	if (use_local) {
		layer >>= 20;
	}

	return layer;
}

std::vector<BL::Object> DataExporter::getObjectList(const std::string ob_name, const std::string group_name)
{
	std::vector<BL::Object> objects, additional;

	BL::Scene::objects_iterator obIt;
	for (m_scene.objects.begin(obIt); obIt != m_scene.objects.end(); ++obIt) {
		BL::Object ob(*obIt);
		if (ob.name() == ob_name) {
			objects.push_back(ob);
			break;
		}
	}

	BL::BlendData::groups_iterator grIt;
	for (m_data.groups.begin(grIt); grIt != m_data.groups.end(); ++grIt) {
		BL::Group gr(*grIt);
		if (gr.name() == group_name) {
			BL::Group::objects_iterator grObIt;
			for (gr.objects.begin(grObIt); grObIt != gr.objects.end(); ++grObIt) {
				BL::Object ob = *grObIt;
				objects.push_back(ob);
			}
			break;
		}
	}

	// check an object is a group instance and hide the group members
	for (auto & ob : objects) {
		const auto otype = ob.dupli_type();
		const auto oname = ob.name();
		if (ob.dupli_type() == BL::Object::dupli_type_GROUP) {
			BL::Group group = ob.dupli_group();

			BL::Group::objects_iterator grObIt;
			for (group.objects.begin(grObIt); grObIt != group.objects.end(); ++grObIt) {
				additional.push_back(*grObIt);
			}
		}
	}

	// merge objects
	objects.insert(objects.end(), additional.begin(), additional.end());

	return objects;
}

void DataExporter::setActiveCamera(BL::Camera camera)
{
	m_active_camera = camera;
}

void DataExporter::refreshHideLists()
{
	m_hide_lists.clear();

	if (!m_active_camera) {
		PRINT_WARN("No active camera set in DataExporter!");
		return;
	}

	auto cameraData = BL::Object(m_active_camera).data().ptr;
	PointerRNA vrayCamera = RNA_pointer_get(&cameraData, "vray");
	if (!RNA_boolean_get(&vrayCamera, "hide_from_view")) {
		return;
	}

	static const std::string typeNames[] = {"camera", "gi", "reflect", "refract", "shadows"};
	std::vector<BL::Object> autoObjects;
	bool autoObjectsInit = false;

	for (const auto & type : typeNames) {
		if (!RNA_boolean_get(&vrayCamera, ("hf_" + type).c_str())) {
			continue;
		}

		if (RNA_boolean_get(&vrayCamera, ("hf_" + type + "_auto").c_str())) {
			if (!autoObjectsInit) {
				autoObjectsInit = true;
				autoObjects = getObjectList("", "hf_" + m_active_camera.name());
			}
			m_hide_lists[type] = autoObjects;
		} else {
			m_hide_lists[type] = getObjectList(RNA_std_string_get(&vrayCamera, "hf_" + type + "_objects"), RNA_std_string_get(&vrayCamera, "hf_" + type + "_groups"));
		}
	}
}


bool DataExporter::isObjectInHideList(BL::Object ob, const std::string listName) const
{
	auto list = m_hide_lists.find(listName);
	if (list != m_hide_lists.end()) {
		return list->second.end() != std::find(list->second.begin(), list->second.end(), ob);
	}
	return false;
}

AttrValue DataExporter::exportObject(BL::Object ob, bool check_updated, const ObjectOverridesAttrs & override)
{
	AttrPlugin  node;

	BL::ID data(ob.data());
	if (data) {
		AttrPlugin  geom;
		AttrPlugin  mtl;

		bool is_updated      = check_updated ? ob.is_updated()      : true;
		bool is_data_updated = check_updated ? ob.is_updated_data() : true;

		// we are syncing dupli, without instancer -> we need to export the node
		if (override && !override.useInstancer) {
			is_updated = true;
		}

		// we are syncing "undo" state so check if this object was changed in the "do" state
		if (!is_updated && shouldSyncUndoneObject(ob)) {
			is_updated = true;
		}

		if (!is_updated && ob.parent()) {
			BL::Object parent(ob.parent());
			is_updated = parent.is_updated();
		}
		if (!is_data_updated && ob.parent()) {
			BL::Object parent(ob.parent());
			is_data_updated = parent.is_updated_data();
		}

		BL::NodeTree ntree = Nodes::GetNodeTree(ob);
		if (ntree) {
			is_data_updated |= ntree.is_updated();
			DataExporter::tag_ntree(ntree, false);
		}

		bool isMeshLight = false;

		// XXX: Check for valid mesh?

		if (!ntree) {
			// TODO: Add check for export meshes flag (m_settings.export_meshes)

			if (!is_data_updated && !m_layer_changed) {
				// nothing changed just get the name
				geom = AttrPlugin(getMeshName(ob));
			} else if (is_data_updated) {
				// data was updated - must export mesh
				geom = exportGeomStaticMesh(ob, override);
				if (!geom) {
					PRINT_ERROR("Object: %s => Incorrect geometry!", ob.name().c_str());
				}
			} else if (m_layer_changed) {
				// changed layer, maybe this object's geom is still not exported
				const auto name = getMeshName(ob);
				if (m_exporter->getPluginManager().inCache(name)) {
					geom = AttrPlugin(name);
				} else {
					geom = exportGeomStaticMesh(ob, override);
					if (!geom) {
						PRINT_ERROR("Object: %s => Incorrect geometry!", ob.name().c_str());
					}
				}
			}

			if (is_updated || m_layer_changed) {
				// NOTE: It's easier just to reexport full material
				mtl = exportMtlMulti(ob);
			}
		}
		else {
			// Export object data from node tree
			//
			BL::Node nodeOutput = Nodes::GetNodeByType(ntree, "VRayNodeObjectOutput");
			if (!nodeOutput) {
				PRINT_ERROR("Object: %s Node tree: %s => Output node not found!",
					ob.name().c_str(), ntree.name().c_str());
			}
			else {
				BL::NodeSocket geometrySocket = Nodes::GetInputSocketByName(nodeOutput, "Geometry");
				if (!(geometrySocket && geometrySocket.is_linked())) {
					PRINT_ERROR("Object: %s Node tree: %s => Geometry node is not set!",
						ob.name().c_str(), ntree.name().c_str());
				}
				else {
					NodeContext context(m_data, m_scene, ob);

					geom = DataExporter::exportSocket(ntree, geometrySocket, context);
					if (!geom) {
						PRINT_ERROR("Object: %s Node tree: %s => Incorrect geometry!",
							ob.name().c_str(), ntree.name().c_str());
					}
					else {
						BL::Node geometryNode = DataExporter::getConnectedNode(ntree, geometrySocket, context);

						isMeshLight = geometryNode.bl_idname() == "VRayNodeLightMesh";

						// Check if connected node is a LightMesh,
						// if so there is no need to export materials
						if (isMeshLight) {
							// Add LightMesh plugin to plugins generated by current object
							m_id_track.insert(ob, geom.plugin);
						}
						else {
							BL::NodeSocket materialSocket = Nodes::GetInputSocketByName(nodeOutput, "Material");
							if (!(materialSocket && materialSocket.is_linked())) {
								PRINT_ERROR("Object: %s Node tree: %s => Material node is not set! Using object materials.",
									ob.name().c_str(), ntree.name().c_str());

								// Use existing object materials
								mtl = exportMtlMulti(ob);
							}
							else {
								mtl = DataExporter::exportSocket(ntree, materialSocket, context);
								if (!mtl) {
									PRINT_ERROR("Object: %s Node tree: %s => Incorrect material!",
										ob.name().c_str(), ntree.name().c_str());
								}
							}
						}
					}
				}
			}
		}

		const std::string & exportName = override.namePrefix + getNodeName(ob);

		// Add Node plugin to plugins generated by current object
		if (override) {
			// we have dupli
		} else {
			m_id_track.insert(ob, exportName);
		}

		// If no material is generated use default or override
		if (!mtl) {
			mtl = getDefaultMaterial();
		}

		if (geom && mtl && (is_updated || is_data_updated || m_layer_changed)) {
			// No need to export Node if the object is LightMesh
			if (!isMeshLight) {
				PluginDesc nodeDesc(exportName, "Node");
				nodeDesc.add("geometry", geom);
				nodeDesc.add("material", mtl);
				nodeDesc.add("objectID", ob.pass_index());
				if (override) {
					nodeDesc.add("visible", override.visible);
					nodeDesc.add("transform", override.tm);
				}
				else {
					nodeDesc.add("transform", AttrTransformFromBlTransform(ob.matrix_world()));
					nodeDesc.add("visible", isObjectVisible(ob));
				}

				node = m_exporter->export_plugin(nodeDesc);
			}
		}
	}

	return node;
}

AttrValue DataExporter::exportVRayClipper(BL::Object ob, bool check_updated, const ObjectOverridesAttrs &overrideAttrs) 
{
	PointerRNA vrayObject  = RNA_pointer_get(&ob.ptr, "vray");
	PointerRNA vrayClipper = RNA_pointer_get(&vrayObject, "VRayClipper");

	const auto clipNode = overrideAttrs.namePrefix + getNodeName(ob);
	const std::string &pluginName = "Clipper@" + clipNode;
	m_id_track.insert(ob, pluginName, IdTrack::CLIPPER);

	bool is_updated      = check_updated ? ob.is_updated()      : true;
	bool is_data_updated = check_updated ? ob.is_updated_data() : true;

	if (!is_updated && shouldSyncUndoneObject(ob)) {
		is_updated = true;
	}

	if (!is_updated && !is_data_updated && !m_layer_changed) {
		return pluginName;
	}

	auto material = exportMtlMulti(ob);

	PluginDesc nodeDesc(pluginName, "VRayClipper");

	if (material) {
		nodeDesc.add("material", material);
	}

	if (RNA_boolean_get(&vrayClipper, "use_obj_mesh")) {
		nodeDesc.add("clip_mesh", AttrPlugin(clipNode));
	} else {
		nodeDesc.add("clip_mesh", AttrPlugin("NULL"));
	}
	nodeDesc.add("enabled", 1);
	nodeDesc.add("affect_light", RNA_boolean_get(&vrayClipper, "affect_light"));
	nodeDesc.add("only_camera_rays", RNA_boolean_get(&vrayClipper, "only_camera_rays"));
	nodeDesc.add("clip_lights", RNA_boolean_get(&vrayClipper, "clip_lights"));
	nodeDesc.add("use_obj_mtl", RNA_boolean_get(&vrayClipper, "use_obj_mtl"));
	nodeDesc.add("set_material_id", RNA_boolean_get(&vrayClipper, "set_material_id"));
	nodeDesc.add("material_id", RNA_int_get(&vrayClipper, "material_id"));
	nodeDesc.add("object_id", ob.pass_index());
	if (overrideAttrs) {
		nodeDesc.add("transform", overrideAttrs.tm);
	} else {
		nodeDesc.add("transform", AttrTransformFromBlTransform(ob.matrix_world()));
	}

	const std::string &excludeGroupName = RNA_std_string_get(&vrayClipper, "exclusion_nodes");
	if (NOT(excludeGroupName.empty())) {
		AttrListPlugin plList;
		BL::BlendData::groups_iterator grIt;
		for (m_data.groups.begin(grIt); grIt != m_data.groups.end(); ++grIt) {
			BL::Group gr = *grIt;
			if (gr.name() == excludeGroupName) {
				BL::Group::objects_iterator grObIt;
				for (gr.objects.begin(grObIt); grObIt != gr.objects.end(); ++grObIt) {
					BL::Object ob = *grObIt;
					plList.append(getNodeName(ob));
				}
				break;
			}
		}

		nodeDesc.add("exclusion_mode", RNA_enum_get(&vrayClipper, "exclusion_mode"));
		nodeDesc.add("exclusion_nodes", plList);
	}


	return m_exporter->export_plugin(nodeDesc);
}

void DataExporter::exportHair(BL::Object ob, BL::ParticleSystemModifier psm, BL::ParticleSystem psys, bool check_updated)
{
	bool is_updated      = check_updated ? ob.is_updated()      : true;
	bool is_data_updated = check_updated ? ob.is_updated_data() : true;

	if (!is_updated && shouldSyncUndoneObject(ob)) {
		is_updated = true;
	}

	BL::ParticleSettings pset(psys.settings());
	if (pset && (pset.type() == BL::ParticleSettings::type_HAIR) && (pset.render_type() == BL::ParticleSettings::render_type_PATH)) {
		const int hair_is_updated = check_updated
						            ? (is_updated || pset.is_updated())
						            : true;

		const int hair_is_data_updated = check_updated
						                 ? (is_data_updated || pset.is_updated())
						                 : true;

		const std::string hairNodeName = "Node@" + getHairName(ob, psys, pset);

		using Visibility = ObjectVisibility;

		const int base_visibility = Visibility::HIDE_VIEWPORT | Visibility::HIDE_RENDER | Visibility::HIDE_LAYER;
		const bool skip_export = !isObjectVisible(ob, Visibility(base_visibility));

		if (skip_export) {
			m_exporter->remove_plugin(hairNodeName);
		} else {
			// Put hair node to the object dependent plugines
			// (will be used to remove plugin when object is removed)
			m_id_track.insert(ob, hairNodeName, IdTrack::HAIR);

			AttrValue hair_geom;
			const auto exporthairName = getHairName(ob, psys, pset);

			// TODO: Add check for export meshes flag (m_settings.export_meshes)
			if (!hair_is_data_updated && !m_layer_changed) {
				// nothing changed just get the name
				hair_geom = AttrPlugin(getMeshName(ob));
			} else if (is_data_updated) {
				// data was updated - must export mesh
				hair_geom = exportGeomMayaHair(ob, psys, psm);
				if (!hair_geom) {
					PRINT_ERROR("Object: %s => Incorrect hair geometry!", ob.name().c_str());
				}
			} else if (m_layer_changed) {
				// changed layer, maybe hair's geom is still not exported
				if (m_exporter->getPluginManager().inCache(exporthairName)) {
					hair_geom = AttrPlugin(exporthairName);
				} else {
					hair_geom = exportGeomMayaHair(ob, psys, psm);
					if (!hair_geom) {
						PRINT_ERROR("Object: %s => Incorrect geometry!", ob.name().c_str());
					}
				}
			}

			AttrValue hair_mtl;
			const int hair_mtl_index = pset.material() - 1;
			if (ob.material_slots.length() && (hair_mtl_index < ob.material_slots.length())) {
				BL::Material hair_material = ob.material_slots[hair_mtl_index].material();
				if (hair_material) {
					hair_mtl = exportMaterial(hair_material, ob);
				}
			}
			if (!hair_mtl) {
				hair_mtl = getDefaultMaterial();
			}

			if (hair_geom && hair_mtl && (hair_is_updated || hair_is_data_updated || m_layer_changed)) {
				PluginDesc hairNodeDesc(hairNodeName, "Node");
				hairNodeDesc.add("geometry", hair_geom);
				hairNodeDesc.add("material", hair_mtl);
				hairNodeDesc.add("transform", AttrTransformFromBlTransform(ob.matrix_world()));
				hairNodeDesc.add("objectID", ob.pass_index());

				m_exporter->export_plugin(hairNodeDesc);
			}
		}
	}
}


AttrValue DataExporter::exportVrayInstacer2(BL::Object ob, AttrInstancer & instacer, IdTrack::PluginType dupliType, bool exportObTm)
{
	const auto exportName = "Instancer2@" + getNodeName(ob);
	// track instancer
	m_id_track.insert(ob, exportName, dupliType);
	const bool visible = isObjectVisible(ob, ObjectVisibility(HIDE_RENDER | HIDE_VIEWPORT));

	PluginDesc instancerDesc(exportName, "Instancer2");
	instancerDesc.add("instances", instacer);
	instancerDesc.add("visible", visible);
	instancerDesc.add("use_time_instancing", false);
	instancerDesc.add("shading_needs_ids", true);

	const auto & wrapperName = "NodeWrapper@" + exportName;
	// also track node wrapper
	m_id_track.insert(ob, wrapperName, dupliType);
	PluginDesc nodeWrapper(wrapperName, "Node");

	auto inst = m_exporter->export_plugin(instancerDesc);
	nodeWrapper.add("geometry", inst);
	nodeWrapper.add("visible", true);
	nodeWrapper.add("objectID", ob.pass_index());
	nodeWrapper.add("material", getDefaultMaterial());
	if (exportObTm) {
		nodeWrapper.add("transform", AttrTransformFromBlTransform(ob.matrix_world()));
	} else {
		VRayBaseTypes::AttrTransform tm;
		memset(&tm, 0, sizeof(tm));
		tm.m.v0.x = 1.f;
		tm.m.v1.y = 1.f;
		tm.m.v2.z = 1.f;
		nodeWrapper.add("transform", tm);
	}

	return m_exporter->export_plugin(nodeWrapper);
}