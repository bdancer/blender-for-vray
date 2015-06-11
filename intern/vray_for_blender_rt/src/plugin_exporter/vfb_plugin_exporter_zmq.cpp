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

#include "vfb_plugin_exporter_zmq.h"
#include "vfb_export_settings.h"
#include "jpeglib.h"

static std::mutex imgMutex;

using namespace VRayForBlender;

struct JpegErrorManager {
	jpeg_error_mgr pub;
	jmp_buf setjmp_buffer;
};

static void jpegErrorExit(j_common_ptr cinfo) {
	JpegErrorManager * myerr = (JpegErrorManager*)cinfo->err;
	(*cinfo->err->output_message) (cinfo);
	longjmp(myerr->setjmp_buffer, 1);
}

static float * jpegToPixelData(unsigned char * data, int size) {
	jpeg_decompress_struct jpegInfo;
	JpegErrorManager jpegError;

	jpegInfo.err = jpeg_std_error(&jpegError.pub);

	jpegError.pub.error_exit = jpegErrorExit;

	if (setjmp(jpegError.setjmp_buffer)) {
		jpeg_destroy_decompress(&jpegInfo);
		return nullptr;
	}

	jpeg_create_decompress(&jpegInfo);
	jpeg_mem_src(&jpegInfo, data, size);

	if (jpeg_read_header(&jpegInfo, TRUE) != JPEG_HEADER_OK) {
		return nullptr;
	}

	jpegInfo.out_color_space = JCS_EXT_RGBA;

	if (!jpeg_start_decompress(&jpegInfo)) {
		return nullptr;
	}

	int rowStride = jpegInfo.output_width * jpegInfo.output_components;
	float * imageData = new float[jpegInfo.output_height * rowStride];
	JSAMPARRAY buffer = (*jpegInfo.mem->alloc_sarray)((j_common_ptr)&jpegInfo, JPOOL_IMAGE, rowStride, 1);

	int c = 0;
	while (jpegInfo.output_scanline < jpegInfo.output_height) {
		jpeg_read_scanlines(&jpegInfo, buffer, 1);

		float * dest = imageData + c * rowStride;
		unsigned char * source = buffer[0];

		for (int r = 0; r < jpegInfo.image_width * jpegInfo.output_components; ++r) {
			dest[r] = source[r] / 255.f;
		}

		++c;
	}

	jpeg_finish_decompress(&jpegInfo);
	jpeg_destroy_decompress(&jpegInfo);

	return imageData;
}

void ZmqRenderImage::update(const VRayMessage & msg) {
	auto img = msg.getValue<VRayBaseTypes::AttrImage>();

	if (img->imageType == VRayBaseTypes::AttrImage::ImageType::JPG) {
		float * imgData = jpegToPixelData(reinterpret_cast<unsigned char*>(img->data.get()), img->size);

		std::unique_lock<std::mutex> lock(imgMutex);

		this->w = img->width;
		this->h = img->height;
		delete[] pixels;
		this->pixels = imgData;
	} else if (img->imageType == VRayBaseTypes::AttrImage::ImageType::RGBA_REAL) {
		const float * imgData = reinterpret_cast<const float *>(img->data.get());
		float * myImage = new float[img->width * img->height * 4];

		std::unique_lock<std::mutex> lock(imgMutex);

		memcpy(myImage, imgData, img->width * img->height * 4 * sizeof(float));

		this->w = img->width;
		this->h = img->height;
		delete[] pixels;
		this->pixels = myImage;
	}
}


ZmqExporter::ZmqExporter(): m_Client(new ZmqClient())
{
}


ZmqExporter::~ZmqExporter()
{
	stop();
	free();
	m_Client->setFlushOnExit(true);
	delete m_Client;
}


RenderImage ZmqExporter::get_image() {
	RenderImage img;

	if (this->m_CurrentImage.pixels) {
		std::unique_lock<std::mutex> lock(imgMutex);

		img.w = this->m_CurrentImage.w;
		img.h = this->m_CurrentImage.h;
		img.pixels = new float[this->m_CurrentImage.w * this->m_CurrentImage.h * 4];
		memcpy(img.pixels, this->m_CurrentImage.pixels, this->m_CurrentImage.w * this->m_CurrentImage.h * 4 * sizeof(float));
	}

	return img;
}

void ZmqExporter::init()
{
	try {
		m_Client->setCallback([this](VRayMessage & message, ZmqWrapper * client) {
			if (message.getType() == VRayMessage::Type::SingleValue && message.getValueType() == VRayBaseTypes::ValueType::ValueTypeImage) {
				this->m_CurrentImage.update(message);
				if (this->callback_on_rt_image_updated) {
					callback_on_rt_image_updated.cb();
				}
			}
		});
		char portStr[32];
		snprintf(portStr, 32, ":%d", this->m_ServerPort);

		m_Client->connect(("tcp://" + this->m_ServerAddress + portStr).c_str());
		m_Client->send(VRayMessage::createMessage(VRayMessage::RendererAction::Init));
	} catch (zmq::error_t &e) {
		PRINT_ERROR("Failed to initialize ZMQ client\n%s", e.what());
	}
}

void ZmqExporter::set_settings(const ExporterSettings & settings) {
	this->m_ServerPort = settings.zmq_server_port;
	this->m_ServerAddress = settings.zmq_server_address;
}


void ZmqExporter::free()
{
	m_Client->send(VRayMessage::createMessage(VRayMessage::RendererAction::Free));
}

void ZmqExporter::sync()
{
}

void ZmqExporter::set_render_size(const int &w, const int &h) {
	m_Client->send(VRayMessage::createMessage(VRayMessage::RendererAction::Resize, w, h));
}

void ZmqExporter::start()
{
	m_Client->send(VRayMessage::createMessage(VRayMessage::RendererAction::Start));
}


void ZmqExporter::stop()
{
	m_Client->send(VRayMessage::createMessage(VRayMessage::RendererAction::Stop));
}


AttrPlugin ZmqExporter::export_plugin(const PluginDesc & pDesc)
{
	const auto & pluginDesc = m_PluginManager.filterPlugin(pDesc);

	if (pluginDesc.pluginID.empty()) {
		PRINT_WARN("[%s] PluginDesc.pluginID is not set!",
			pluginDesc.pluginName.c_str());
		return AttrPlugin();
	}
	const std::string & name = pluginDesc.pluginName;

	m_Client->send(VRayMessage::createMessage(name, pluginDesc.pluginID));

	bool timeSet = false;
	float lastTime = 0;

	for (auto & attributePairs : pluginDesc.pluginAttrs) {
		const PluginAttr & attr = attributePairs.second;
		PRINT_INFO_EX("Updating: \"%s\" => %s.%s",
			name.c_str(), pluginDesc.pluginID.c_str(), attr.attrName.c_str());

		if (!timeSet) {
			m_Client->send(VRayMessage::createMessage(VRayMessage::RendererAction::SetCurrentTime, attr.time));
			timeSet = true;
		} else if (lastTime != attr.time) {
			m_Client->send(VRayMessage::createMessage(VRayMessage::RendererAction::SetCurrentTime, attr.time));
			lastTime = attr.time;
		}

		switch (attr.attrValue.type) {
		case ValueTypeUnknown:
			break;
		case ValueTypeInt:
			m_Client->send(VRayMessage::createMessage(name, attr.attrName, VRayBaseTypes::AttrSimpleType<int>(attr.attrValue.valInt)));
			break;
		case ValueTypeFloat:
			m_Client->send(VRayMessage::createMessage(name, attr.attrName, VRayBaseTypes::AttrSimpleType<float>(attr.attrValue.valFloat)));
			break;
		case ValueTypeString:
			m_Client->send(VRayMessage::createMessage(name, attr.attrName, VRayBaseTypes::AttrSimpleType<std::string>(attr.attrValue.valString)));
			break;
		case ValueTypeColor:
			m_Client->send(VRayMessage::createMessage(name, attr.attrName, attr.attrValue.valColor));
			break;
		case ValueTypeVector:
			m_Client->send(VRayMessage::createMessage(name, attr.attrName, attr.attrValue.valVector));
			break;
		case ValueTypeAColor:
			m_Client->send(VRayMessage::createMessage(name, attr.attrName, attr.attrValue.valAColor));
			break;
		case ValueTypePlugin:
			m_Client->send(VRayMessage::createMessage(name, attr.attrName, attr.attrValue.valPlugin));
			break;
		case ValueTypeTransform:
			m_Client->send(VRayMessage::createMessage(name, attr.attrName, attr.attrValue.valTransform));
			break;
		case ValueTypeListInt:
			m_Client->send(VRayMessage::createMessage(name, attr.attrName, attr.attrValue.valListInt));
			break;
		case ValueTypeListFloat:
			m_Client->send(VRayMessage::createMessage(name, attr.attrName, attr.attrValue.valListFloat));
			break;
		case ValueTypeListVector:
			m_Client->send(VRayMessage::createMessage(name, attr.attrName, attr.attrValue.valListVector));
			break;
		case ValueTypeListColor:
			m_Client->send(VRayMessage::createMessage(name, attr.attrName, attr.attrValue.valListColor));
			break;
		case ValueTypeListPlugin:
			m_Client->send(VRayMessage::createMessage(name, attr.attrName, attr.attrValue.valListPlugin));
			break;
		case ValueTypeListString:
			m_Client->send(VRayMessage::createMessage(name, attr.attrName, attr.attrValue.valListString));
			break;
		case ValueTypeMapChannels:
			m_Client->send(VRayMessage::createMessage(name, attr.attrName, attr.attrValue.valMapChannels));
			break;
		case ValueTypeInstancer:
			m_Client->send(VRayMessage::createMessage(name, attr.attrName, attr.attrValue.valInstancer));
			break;
		default:
			PRINT_INFO_EX("--- > UNIMPLEMENTED DEFAULT");
			assert(false);
			break;
		}
	}

	AttrPlugin plugin;
	plugin.plugin = name;

	return plugin;
}
