#include "stdafx.h"
#include "../Common/BufferUtils.h"
#include "../Program/GLSLCommon.h"
#include "../rsx_methods.h"

#include "VKAsyncScheduler.h"
#include "VKGSRender.h"
#include "vkutils/buffer_object.h"
#include "vkutils/chip_class.h"
#include <bit>
#include <type_traits>
#include <vulkan/vulkan_core.h>

#include "Emu/RSX/NV47/HW/context_accessors.define.h"

namespace
{
	template <typename T>
	u64 infamous2_vk_handle(T handle)
	{
		if constexpr (std::is_pointer_v<T>)
		{
			return reinterpret_cast<uptr>(handle);
		}
		else
		{
			return static_cast<u64>(handle);
		}
	}

	// IEEE-754 bits keep the JSON valid even for NaN/Inf and preserve exact values.
	u32 infamous2_float_bits(float value)
	{
		return std::bit_cast<u32>(value);
	}

	std::string infamous2_texture_params_json(const rsx::fragment_program_texture_config::TIU_slot& p)
	{
		return fmt::format("{\"scale_bits\":[%u,%u,%u],\"bias_bits\":[%u,%u,%u],"
			"\"clamp\":%u,\"clamp_min_bits\":[%u,%u],\"clamp_max_bits\":[%u,%u],\"control\":%u,\"remap\":%u}",
			infamous2_float_bits(p.scale[0]), infamous2_float_bits(p.scale[1]), infamous2_float_bits(p.scale[2]),
			infamous2_float_bits(p.bias[0]), infamous2_float_bits(p.bias[1]), infamous2_float_bits(p.bias[2]),
			(p.control >> rsx::texture_control_bits::CLAMP_TEXCOORDS_BIT) & 1,
			infamous2_float_bits(p.clamp_min[0]), infamous2_float_bits(p.clamp_min[1]),
			infamous2_float_bits(p.clamp_max[0]), infamous2_float_bits(p.clamp_max[1]), p.control, p.remap);
	}

	std::string infamous2_image_json(const vk::image* image, bool include_rtt = true)
	{
		if (!image)
		{
			return "null";
		}

		std::string result = fmt::format("{\"image\":%llu,\"uid\":%llu,\"extent\":[%u,%u,%u],\"format\":%u,\"samples\":%u,"
			"\"type\":%u,\"mips\":%u,\"layers\":%u,\"aspect\":%u,\"layout\":%u,\"format_class\":%u",
			infamous2_vk_handle(image->value), image->uid(), image->width(), image->height(), image->depth(),
			static_cast<u32>(image->format()), image->samples(), static_cast<u32>(image->type()),
			image->mipmaps(), image->layers(), image->aspect(), static_cast<u32>(image->current_layout), static_cast<u32>(image->format_class()));
		if (const auto rtt = include_rtt ? vk::try_as_rtt(image) : nullptr)
		{
			result += fmt::format(",\"rtt\":{\"base_addr\":%u,\"guest_extent\":[%u,%u],\"guest_pitch\":%u,\"native_pitch\":%u,"
				"\"scale_percent\":%u,\"min_scalable_dimension\":%u,\"aa_mode\":%u,\"spp\":%u,\"samples_xy\":[%u,%u],\"resolve\":%s}",
				rtt->base_addr, rtt->surface_width, rtt->surface_height, rtt->rsx_pitch, rtt->native_pitch,
				rtt->resolution_scaling_config.scale_percent, rtt->resolution_scaling_config.min_scalable_dimension,
				static_cast<u32>(rtt->aa_mode), rtt->spp, rtt->samples_x, rtt->samples_y,
				infamous2_image_json(rtt->resolve_surface.get(), false));
		}
		return result + '}';
	}

	std::string infamous2_view_json(const vk::image_view* view)
	{
		if (!view)
		{
			return "null";
		}
		const auto image = view->image();
		const auto& range = view->info.subresourceRange;
		const u32 mip = std::min(range.baseMipLevel, 31u);
		return fmt::format("{\"view\":%llu,\"format\":%u,\"type\":%u,\"aspect\":%u,"
			"\"base_mip\":%u,\"mip_count\":%u,\"base_layer\":%u,\"layer_count\":%u,\"components\":[%u,%u,%u,%u],"
			"\"extent_at_base_mip\":[%u,%u,%u],\"resource\":%s}",
			infamous2_vk_handle(view->value), static_cast<u32>(view->info.format), static_cast<u32>(view->info.viewType),
			range.aspectMask, range.baseMipLevel, range.levelCount, range.baseArrayLayer, range.layerCount,
			static_cast<u32>(view->info.components.r), static_cast<u32>(view->info.components.g),
			static_cast<u32>(view->info.components.b), static_cast<u32>(view->info.components.a),
			image ? std::max(1u, image->width() >> mip) : 0, image ? std::max(1u, image->height() >> mip) : 0,
			image ? std::max(1u, image->depth() >> mip) : 0, infamous2_image_json(image));
	}
}

namespace vk
{
	VkImageViewType get_view_type(rsx::texture_dimension_extended type)
	{
		switch (type)
		{
		case rsx::texture_dimension_extended::texture_dimension_1d:
			return VK_IMAGE_VIEW_TYPE_1D;
		case rsx::texture_dimension_extended::texture_dimension_2d:
			return VK_IMAGE_VIEW_TYPE_2D;
		case rsx::texture_dimension_extended::texture_dimension_cubemap:
			return VK_IMAGE_VIEW_TYPE_CUBE;
		case rsx::texture_dimension_extended::texture_dimension_3d:
			return VK_IMAGE_VIEW_TYPE_3D;
		default: fmt::throw_exception("Unreachable");
		}
	}

	VkCompareOp get_compare_func(rsx::comparison_function op, bool reverse_direction = false)
	{
		switch (op)
		{
		case rsx::comparison_function::never: return VK_COMPARE_OP_NEVER;
		case rsx::comparison_function::greater: return reverse_direction ? VK_COMPARE_OP_LESS: VK_COMPARE_OP_GREATER;
		case rsx::comparison_function::less: return reverse_direction ? VK_COMPARE_OP_GREATER: VK_COMPARE_OP_LESS;
		case rsx::comparison_function::less_or_equal: return reverse_direction ? VK_COMPARE_OP_GREATER_OR_EQUAL: VK_COMPARE_OP_LESS_OR_EQUAL;
		case rsx::comparison_function::greater_or_equal: return reverse_direction ? VK_COMPARE_OP_LESS_OR_EQUAL: VK_COMPARE_OP_GREATER_OR_EQUAL;
		case rsx::comparison_function::equal: return VK_COMPARE_OP_EQUAL;
		case rsx::comparison_function::not_equal: return VK_COMPARE_OP_NOT_EQUAL;
		case rsx::comparison_function::always: return VK_COMPARE_OP_ALWAYS;
		default:
			fmt::throw_exception("Unknown compare op: 0x%x", static_cast<u32>(op));
		}
	}

	void validate_image_layout_for_read_access(
		vk::command_buffer& cmd,
		vk::image_view* view,
		VkPipelineStageFlags dst_stage,
		const rsx::sampled_image_descriptor_base* sampler_state)
	{
		switch (auto raw = view->image(); +raw->current_layout)
		{
		default:
			//case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			//ensure(sampler_state->upload_context == rsx::texture_upload_context::blit_engine_dst);
			raw->change_layout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			ensure(sampler_state->upload_context == rsx::texture_upload_context::blit_engine_src);
			raw->change_layout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			break;
		case VK_IMAGE_LAYOUT_GENERAL:
		case VK_IMAGE_LAYOUT_ATTACHMENT_FEEDBACK_LOOP_OPTIMAL_EXT:
			ensure(sampler_state->upload_context == rsx::texture_upload_context::framebuffer_storage);
			if (sampler_state->is_cyclic_reference) [[ unlikely ]]
			{
				// Nothing to do
				break;
			}

			// This was used in a cyclic ref before, but is missing a barrier
			// No need for a full stall, use a custom barrier instead
			VkPipelineStageFlags src_stage;
			VkAccessFlags src_access, dst_access;
			if (raw->aspect() == VK_IMAGE_ASPECT_COLOR_BIT)
			{
				src_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				src_access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				dst_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
				dst_stage |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			}
			else
			{
				src_stage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
				src_access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
				dst_access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
				dst_stage |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			}

			vk::insert_image_memory_barrier(
				cmd,
				raw->value,
				raw->current_layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				src_stage, dst_stage,
				src_access, dst_access,
				{ raw->aspect(), 0, 1, 0, 1 });

			raw->current_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			break;
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			ensure(sampler_state->upload_context == rsx::texture_upload_context::framebuffer_storage);
			if (!sampler_state->is_cyclic_reference) [[ likely ]]
			{
				// Standard pre-read barrier.
				raw->change_layout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				break;
			}

			// Normally this shouldn't happen. But that is only guaranteed if the attachment state never changes outside of RTT rebind interrupts.
			// If the shader changes between binds to "disable" the cyclic nature, we could end up here.
			// Draw 1 (cyllic) -> texture_barrier -> Draw 2 (no textures) -> attachment_optimal -> Draw 3 (cylic again, no new data) -> incorrect layout.
			vk::as_rtt(raw)->texture_barrier(cmd);
			break;
		}
	}
}

bool VKGSRender::infamous2_capture_vk_candidate() const
{
	const auto capture = get_infamous2_capture_context();
	if (!capture.active || m_infamous2_vk_budget_exhausted_capture_id == capture.id)
	{
		return false;
	}

	const auto& r = rsx::method_registers;
	const u32 w = r.surface_clip_width();
	const u32 h = r.surface_clip_height();
	// A state catalog, not a semantic identification of fire, smoke or depth effects.
	return (w == 512 && h == 288) || (w == 1280 && h == 180) || (w == 320 && h == 180) ||
		(w == 1280 && h == 720 && r.blend_enabled_mask() && !r.depth_write_enabled());
}

void VKGSRender::record_infamous2_capture_vk(const char* kind, const std::string& fields)
{
	const auto capture = get_infamous2_capture_context();
	if (!capture.active || m_infamous2_vk_budget_exhausted_capture_id == capture.id)
	{
		return;
	}
	u64 event_index = 0;
	if (!reserve_infamous2_capture_backend_record(event_index))
	{
		m_infamous2_vk_budget_exhausted_capture_id = capture.id;
		return;
	}

	// The cursor and logical batch are hints at this backend hook. They are not
	// a 1:1 GPU draw mapping; a clause can merge ranges or emit several commands.
	rsx_log.notice("INFAMOUS2_CAPTURE_VK {\"schema\":1,\"capture_id\":%llu,\"event_index\":%llu,"
		"\"logical_batch_index_hint\":%llu,\"command_count\":%llu,\"rrc_cursor\":%llu,\"kind\":\"%s\",%s}",
		capture.id, event_index, capture.next_logical_batch, capture.commands, capture.rrc_cursor, kind, fields);
}

void VKGSRender::record_infamous2_capture_vk_ubo(bool updated)
{
	const auto capture = get_infamous2_capture_context();
	if (!capture.active)
	{
		return;
	}
	if (updated)
	{
		// Cache only existing host parameters, never guest shader/texture memory.
		m_infamous2_vk_ubo_snapshot = current_fragment_program.texture_params;
		m_infamous2_vk_ubo_capture_id = capture.id;
		m_infamous2_vk_ubo_mask = current_fp_metadata.referenced_textures_mask;
	}
	if (!m_infamous2_vk_candidate || m_infamous2_vk_budget_exhausted_capture_id == capture.id)
	{
		return;
	}

	std::string params = "[";
	for (u32 refs = current_fp_metadata.referenced_textures_mask, i = 0; refs; refs >>= 1, ++i)
	{
		if (!(refs & 1)) continue;
		if (params.size() > 1) params += ',';
		params += fmt::format("{\"sampler\":%u,\"params\":%s}", i,
			infamous2_texture_params_json(current_fragment_program.texture_params[i]));
	}
	params += ']';
	record_infamous2_capture_vk("ubo", fmt::format("\"updated\":%u,\"referenced_texture_mask\":%u,"
		"\"buffer\":%llu,\"buffer_offset\":%llu,\"dynamic_offset\":%llu,\"range\":%llu,\"params\":%s,"
		"\"snapshot_current_capture\":%u,\"snapshot_mask\":%u",
		static_cast<u32>(updated), current_fp_metadata.referenced_textures_mask,
		infamous2_vk_handle(m_fragment_texture_params_buffer_info.buffer), m_fragment_texture_params_buffer_info.offset,
		m_texture_parameters_dynamic_offset, m_fragment_texture_params_buffer_info.range, params,
		static_cast<u32>(m_infamous2_vk_ubo_capture_id == capture.id), m_infamous2_vk_ubo_mask));
}

void VKGSRender::record_infamous2_capture_vk_texture(u32 sampler, vk::image_view* view, const char* path, bool deferred,
	vk::image_view* primary_view, const vk::sampler* bound_sampler)
{
	const auto capture = get_infamous2_capture_context();
	if (!m_infamous2_vk_candidate || !capture.active || m_infamous2_vk_budget_exhausted_capture_id == capture.id)
	{
		return;
	}

	const auto& tex = rsx::method_registers.fragment_textures[sampler];
	const auto desc = static_cast<const vk::texture_cache::sampled_image_descriptor*>(fs_sampler_state[sampler].get());
	const bool valid = tex.enabled() && desc && desc->validate();
	std::string descriptor = "null";
	std::string source = "null";
	std::string reference_surface = "null";
	u32 copy_count = 0;
	if (valid)
	{
		const auto& x = desc->texcoord_xform;
		// The disabled clamp arrays need not be initialized by descriptor constructors.
		const std::string clamp = x.clamp ? fmt::format("{\"min_bits\":[%u,%u],\"max_bits\":[%u,%u]}",
			infamous2_float_bits(x.clamp_min[0]), infamous2_float_bits(x.clamp_min[1]),
			infamous2_float_bits(x.clamp_max[0]), infamous2_float_bits(x.clamp_max[1])) : "null";
		descriptor = fmt::format("{\"upload_context\":%u,\"ref_address\":%u,\"image_type\":%u,\"format_class\":%u,"
			"\"samples\":%u,\"cyclic\":%u,\"surface_cache_tag\":%llu,\"format_bits\":%u,\"host_features\":%u,"
			"\"scale_bits\":[%u,%u,%u],\"bias_bits\":[%u,%u,%u],\"clamp\":%u,\"clamp_bounds\":%s}",
			static_cast<u32>(desc->upload_context), desc->ref_address, static_cast<u32>(desc->image_type), static_cast<u32>(desc->format_class),
			desc->samples, static_cast<u32>(desc->is_cyclic_reference), desc->surface_cache_tag, desc->format_ex.format_bits, desc->format_ex.host_features,
			infamous2_float_bits(x.scale[0]), infamous2_float_bits(x.scale[1]), infamous2_float_bits(x.scale[2]),
			infamous2_float_bits(x.bias[0]), infamous2_float_bits(x.bias[1]), infamous2_float_bits(x.bias[2]), static_cast<u32>(x.clamp), clamp);
		if (deferred)
		{
			const auto& sub = desc->external_subresource_desc;
			copy_count = ::size32(sub.sections_to_copy);
			source = fmt::format("{\"operation\":%u,\"desired_extent\":[%u,%u,%u],\"address\":%u,\"pitch\":%u,"
				"\"external_ref_addr\":%u,\"copy_region_count\":%u,\"copy_regions_limited\":%u,\"src0\":%s}",
				static_cast<u32>(sub.op), sub.width, sub.height, sub.depth, sub.address, sub.pitch, sub.external_ref_addr,
				copy_count, static_cast<u32>(copy_count > 8), infamous2_image_json(sub.src0()));
		}
		if (desc->upload_context == rsx::texture_upload_context::framebuffer_storage && desc->ref_address)
		{
			// Read-only exact-address cache lookup. Presence alone does not prove lineage.
			if (const auto rtt = m_rtts.get_surface_at(desc->ref_address))
			{
				const auto host = view ? view->image() : nullptr;
				reference_surface = fmt::format("{\"matches_image\":%u,\"matches_resolve\":%u,\"resource\":%s}",
					static_cast<u32>(host == rtt), static_cast<u32>(host && host == rtt->resolve_surface.get()), infamous2_image_json(rtt));
			}
		}
	}

	const bool snapshot_valid = m_infamous2_vk_ubo_capture_id == capture.id && (m_infamous2_vk_ubo_mask & (1u << sampler));
	std::string host_sampler = "null";
	if (bound_sampler)
	{
		const auto& s = bound_sampler->info;
		host_sampler = fmt::format("{\"handle\":%llu,\"min_filter\":%u,\"mag_filter\":%u,\"mipmap_mode\":%u,\"address_modes\":[%u,%u,%u],"
			"\"unnormalized\":%u,\"compare_enable\":%u,\"compare_op\":%u,\"lod_bias_bits\":%u,\"min_lod_bits\":%u,\"max_lod_bits\":%u}",
			infamous2_vk_handle(bound_sampler->value), static_cast<u32>(s.minFilter), static_cast<u32>(s.magFilter), static_cast<u32>(s.mipmapMode),
			static_cast<u32>(s.addressModeU), static_cast<u32>(s.addressModeV), static_cast<u32>(s.addressModeW),
			s.unnormalizedCoordinates, s.compareEnable, static_cast<u32>(s.compareOp), infamous2_float_bits(s.mipLodBias),
			infamous2_float_bits(s.minLod), infamous2_float_bits(s.maxLod));
	}
	const auto [fp_offset, fp_location] = rsx::method_registers.shader_program_address();
	record_infamous2_capture_vk("texture", fmt::format("\"sampler\":%u,\"binding_attempt\":%u,\"path\":\"%s\",\"deferred\":%u,"
		"\"fp_offset\":%u,\"fp_location\":%u,\"fp_ucode_bytes\":%u,\"fp_hash_available\":0,\"cache_fp_id\":%u,\"fp_module\":%llu,"
		"\"interpreter\":%u,\"referenced_texture_mask\":%u,\"redirected_texture_mask\":%u,\"shadow_texture_mask\":%u,\"msaa_texture_mask\":%u,"
		"\"guest\":{\"enabled\":%u,\"offset\":%u,\"location\":%u,\"extent\":[%u,%u,%u],\"pitch\":%u,\"format\":%u,\"remap\":%u,"
		"\"zfunc\":%u,\"min_filter\":%u,\"mag_filter\":%u,\"wrap\":[%u,%u,%u]},\"host_sampler\":%s,"
		"\"descriptor_valid\":%u,\"descriptor\":%s,\"view\":%s,\"primary_view\":%s,\"deferred_source\":%s,\"reference_surface_at_address\":%s,"
		"\"effective_params\":%s,\"ubo_snapshot_valid\":%u,\"ubo_snapshot\":%s",
		sampler, m_infamous2_vk_binding_attempt, path, static_cast<u32>(deferred), fp_offset, fp_location, current_fp_metadata.program_ucode_length,
		m_fragment_prog ? m_fragment_prog->id : 0, m_fragment_prog ? infamous2_vk_handle(m_fragment_prog->handle) : 0,
		static_cast<u32>(m_shader_interpreter.is_interpreter(m_program)), current_fp_metadata.referenced_textures_mask,
		current_fragment_program.texture_state.redirected_textures, current_fragment_program.texture_state.shadow_textures,
		current_fragment_program.texture_state.multisampled_textures,
		static_cast<u32>(tex.enabled()), tex.offset(), tex.location(), tex.width(), tex.height(), tex.depth(), tex.pitch(), tex.format(), tex.remap(),
		static_cast<u32>(tex.zfunc()), static_cast<u32>(tex.min_filter()), static_cast<u32>(tex.mag_filter()),
		static_cast<u32>(tex.wrap_s()), static_cast<u32>(tex.wrap_t()), static_cast<u32>(tex.wrap_r()), host_sampler,
		static_cast<u32>(valid), descriptor, infamous2_view_json(view), infamous2_view_json(primary_view), source, reference_surface,
		infamous2_texture_params_json(current_fragment_program.texture_params[sampler]), static_cast<u32>(snapshot_valid),
		snapshot_valid ? infamous2_texture_params_json(m_infamous2_vk_ubo_snapshot[sampler]) : "null"));

	for (u32 i = 0; i < std::min(copy_count, 8u) && m_infamous2_vk_budget_exhausted_capture_id != capture.id; ++i)
	{
		const auto& region = desc->external_subresource_desc.sections_to_copy[i];
		record_infamous2_capture_vk("copy_region", fmt::format("\"sampler\":%u,\"binding_attempt\":%u,\"region_index\":%u,"
			"\"base_addr\":%u,\"level\":%u,\"src_rect\":[%u,%u,%u,%u],\"dst_rect\":[%u,%u,%u,%u,%u],\"xform\":%u,\"source\":%s",
			sampler, m_infamous2_vk_binding_attempt, i, region.base_addr, region.level,
			region.src_x, region.src_y, region.src_w, region.src_h, region.dst_x, region.dst_y, region.dst_z, region.dst_w, region.dst_h,
			region.xform, infamous2_image_json(region.src)));
	}
}

void VKGSRender::record_infamous2_capture_vk_draw(u32 sub_index, u32 vertices, bool indexed, u32 instances, u32 first, u32 ranges, const char* api)
{
	if (!m_infamous2_vk_candidate || !get_infamous2_capture_context().active)
	{
		return;
	}
	const auto& clause = rsx::method_registers.current_draw_clause;
	m_infamous2_vk_emitted_commands++;
	if (m_infamous2_vk_budget_exhausted_capture_id == get_infamous2_capture_context().id) return;

	std::string colors = "[";
	for (u32 i = 0; i < m_rtts.m_bound_render_targets.size(); ++i)
	{
		if (i) colors += ',';
		colors += infamous2_image_json(m_rtts.m_bound_render_targets[i].second);
	}
	colors += ']';
	std::string framebuffer_images = "[";
	for (const auto image : m_fbo_images)
	{
		if (framebuffer_images.size() > 1) framebuffer_images += ',';
		framebuffer_images += infamous2_image_json(image);
	}
	framebuffer_images += ']';
	const auto [fp_offset, fp_location] = rsx::method_registers.shader_program_address();
	record_infamous2_capture_vk("draw_emit", fmt::format("\"rsx_sub_index\":%u,\"host_subdraw_id\":%u,\"vertices\":%u,\"indexed\":%u,"
		"\"api\":\"%s\",\"instances\":%u,\"first\":%u,\"pass_count\":%u,\"range_count\":%u,\"host_command_count\":1,\"conditional_hw\":%u,"
		"\"framebuffer\":[%u,%u],\"viewport_bits\":[%u,%u,%u,%u,%u,%u],\"scissor\":[%d,%d,%u,%u],"
		"\"color_attachments\":%s,\"depth_attachment\":%s,\"framebuffer_images\":%s,\"referenced_texture_mask\":%u,"
		"\"fp_offset\":%u,\"fp_location\":%u,\"fp_ucode_bytes\":%u,\"fp_hash_available\":0,\"cache_fp_id\":%u,\"fp_module\":%llu,\"interpreter\":%u",
		sub_index, m_current_draw.subdraw_id, vertices, static_cast<u32>(indexed), api, instances, first, clause.pass_count(), ranges,
		static_cast<u32>(cond_render_ctrl.hw_cond_active), m_draw_fbo->width(), m_draw_fbo->height(),
		infamous2_float_bits(m_viewport.x), infamous2_float_bits(m_viewport.y), infamous2_float_bits(m_viewport.width), infamous2_float_bits(m_viewport.height),
		infamous2_float_bits(m_viewport.minDepth), infamous2_float_bits(m_viewport.maxDepth),
		m_scissor.offset.x, m_scissor.offset.y, m_scissor.extent.width, m_scissor.extent.height,
		colors, infamous2_image_json(m_rtts.m_bound_depth_stencil.second), framebuffer_images, current_fp_metadata.referenced_textures_mask,
		fp_offset, fp_location, current_fp_metadata.program_ucode_length, m_fragment_prog ? m_fragment_prog->id : 0,
		m_fragment_prog ? infamous2_vk_handle(m_fragment_prog->handle) : 0, static_cast<u32>(m_shader_interpreter.is_interpreter(m_program))));
}

void VKGSRender::begin_render_pass()
{
	vk::begin_renderpass(
		*m_current_command_buffer,
		get_render_pass(),
		m_draw_fbo->value,
		{ positionu{0u, 0u}, sizeu{m_draw_fbo->width(), m_draw_fbo->height()} });
}

void VKGSRender::close_render_pass()
{
	vk::end_renderpass(*m_current_command_buffer);
}

VkRenderPass VKGSRender::get_render_pass()
{
	if (!m_cached_renderpass)
	{
		m_cached_renderpass = vk::get_renderpass(*m_device, m_current_renderpass_key);
	}

	return m_cached_renderpass;
}

void VKGSRender::invalidate_render_pass()
{
	// Regenerate renderpass key for the next draw call
	std::vector<u8> input_attachments{};
	if (current_fragment_program.ctrl & RSX_SHADER_CONTROL_PROGRAMMABLE_BLENDING)
	{
		input_attachments.resize(m_draw_buffers.size());
		std::iota(input_attachments.begin(), input_attachments.end(), 0);
	}

	if (const auto key = vk::get_renderpass_key(m_fbo_images, m_current_renderpass_key, input_attachments);
		key != m_current_renderpass_key)
	{
		m_current_renderpass_key = key;
		m_cached_renderpass = VK_NULL_HANDLE;
	}
}

void VKGSRender::update_draw_state()
{
	m_profiler.start();

	// Update conditional dynamic state
	if (rsx::method_registers.current_draw_clause.primitive >= rsx::primitive_type::points &&   // AMD/AMDVLK driver does not like it if you render points without setting line width for some reason
		rsx::method_registers.current_draw_clause.primitive <= rsx::primitive_type::line_strip)
	{
		const float actual_line_width =
			m_device->get_wide_lines_support() ? rsx::method_registers.line_width() * resolution_scaling_config.scale_factor() : 1.f;
		vkCmdSetLineWidth(*m_current_command_buffer, actual_line_width);
	}

	if (rsx::method_registers.blend_enabled_mask())
	{
		// Update blend constants
		auto blend_colors = rsx::get_constant_blend_colors();
		vkCmdSetBlendConstants(*m_current_command_buffer, blend_colors.data());
	}

	if (rsx::method_registers.stencil_test_enabled())
	{
		const bool two_sided_stencil = rsx::method_registers.two_sided_stencil_test_enabled();
		VkStencilFaceFlags face_flag = (two_sided_stencil) ? VK_STENCIL_FACE_FRONT_BIT : VK_STENCIL_FRONT_AND_BACK;

		vkCmdSetStencilWriteMask(*m_current_command_buffer, face_flag, rsx::method_registers.stencil_mask());
		vkCmdSetStencilCompareMask(*m_current_command_buffer, face_flag, rsx::method_registers.stencil_func_mask());
		vkCmdSetStencilReference(*m_current_command_buffer, face_flag, rsx::method_registers.stencil_func_ref());

		if (two_sided_stencil)
		{
			vkCmdSetStencilWriteMask(*m_current_command_buffer, VK_STENCIL_FACE_BACK_BIT, rsx::method_registers.back_stencil_mask());
			vkCmdSetStencilCompareMask(*m_current_command_buffer, VK_STENCIL_FACE_BACK_BIT, rsx::method_registers.back_stencil_func_mask());
			vkCmdSetStencilReference(*m_current_command_buffer, VK_STENCIL_FACE_BACK_BIT, rsx::method_registers.back_stencil_func_ref());
		}
	}

	// The remaining dynamic state should only be set once and we have signals to enable/disable mid-renderpass
	if (!(m_current_command_buffer->flags & vk::command_buffer::cb_reload_dynamic_state))
	{
		// Dynamic state already set
		m_frame_stats.setup_time += m_profiler.duration();
		return;
	}

	if (rsx::method_registers.poly_offset_fill_enabled())
	{
		// offset_bias is the constant factor, multiplied by the implementation factor R
		// offst_scale is the slope factor, multiplied by the triangle slope factor M
		// R is implementation dependent and has to be derived empirically for supported implementations.
		// Lucky for us, only NVIDIA currently supports fixed-point 24-bit depth buffers.

		const auto polygon_offset_scale = rsx::method_registers.poly_offset_scale();
		auto polygon_offset_bias = rsx::method_registers.poly_offset_bias();

		if (!m_draw_fbo->attachments.empty() &&
			m_draw_fbo->depth_format() == VK_FORMAT_D24_UNORM_S8_UINT &&
			is_NVIDIA(vk::get_chip_family()))
		{
			// Empirically derived to be 0.5 * (2^24 - 1) for fixed type on Pascal. The same seems to apply for other NVIDIA GPUs.
			// RSX seems to be using 2^24 - 1 instead making the biases twice as large when using fixed type Z-buffer on NVIDIA.
			// Note, that the formula for floating point is complicated, but actually works out for us.
			// Since the exponent range for a polygon is around 0, and we have 23 (+1) mantissa bits, R just works out to the same range by chance \o/.
			polygon_offset_bias *= 0.5f;
		}

		vkCmdSetDepthBias(*m_current_command_buffer, polygon_offset_bias, 0.f, polygon_offset_scale);
	}
	else
	{
		// Zero bias value - disables depth bias
		vkCmdSetDepthBias(*m_current_command_buffer, 0.f, 0.f, 0.f);
	}

	if (m_device->get_depth_bounds_support())
	{
		f32 bounds_min, bounds_max;
		if (REGS(m_ctx)->depth_bounds_test_enabled())
		{
			// Update depth bounds min/max, saturated into the RSX depth format's range as hardware does
			const auto depth_format = REGS(m_ctx)->surface_depth_fmt();
			bounds_min = rsx::clamp_depth_bounds_value(depth_format, REGS(m_ctx)->depth_bounds_min());
			bounds_max = rsx::clamp_depth_bounds_value(depth_format, REGS(m_ctx)->depth_bounds_max());
		}
		else
		{
			// Avoid special case where min=max and depth bounds (incorrectly) fails
			bounds_min = std::min(0.f, rsx::method_registers.clip_min());
			bounds_max = std::max(1.f, rsx::method_registers.clip_max());
		}

		if (!backend_config.supports_extended_depth_range)
		{
			if (rsx::method_registers.depth_bounds_test_enabled() && requires_depth_range_emulation()) [[ unlikely ]]
			{
				bounds_min = rsx::encode_emulated_depth(bounds_min);
				bounds_max = rsx::encode_emulated_depth(bounds_max);
			}

			bounds_min = std::clamp(bounds_min, 0.f, 1.f);
			bounds_max = std::clamp(bounds_max, 0.f, 1.f);
		}

		vkCmdSetDepthBounds(*m_current_command_buffer, bounds_min, bounds_max);
	}

	bind_viewport();

	m_current_command_buffer->flags &= ~vk::command_buffer::cb_reload_dynamic_state;
	m_graphics_state.clear(rsx::pipeline_state::polygon_offset_state_dirty | rsx::pipeline_state::depth_bounds_state_dirty);
	m_frame_stats.setup_time += m_profiler.duration();
}

void VKGSRender::load_texture_env()
{
	// Load textures
	bool check_for_cyclic_refs = false;
	auto check_surface_cache_sampler_valid = [&](auto descriptor, const auto& tex)
	{
		if (!m_texture_cache.test_if_descriptor_expired(*m_current_command_buffer, m_rtts, descriptor, tex))
		{
			check_for_cyclic_refs |= descriptor->is_cyclic_reference;
			return true;
		}

		return false;
	};

	auto get_border_color = [&](const rsx::Texture auto& tex, bool remap_colorspace)
	{
		return m_device->get_custom_border_color_support().require_border_color_remap
			? tex.remapped_border_color(remap_colorspace)
			: rsx::decode_border_color(tex.border_color(remap_colorspace));
	};

	std::lock_guard lock(m_sampler_mutex);

	for (u32 textures_ref = current_fp_metadata.referenced_textures_mask, i = 0; textures_ref; textures_ref >>= 1, ++i)
	{
		if (!(textures_ref & 1))
		{
			continue;
		}

		if (!fs_sampler_state[i])
		{
			fs_sampler_state[i] = std::make_unique<vk::texture_cache::sampled_image_descriptor>();
		}

		auto sampler_state = static_cast<vk::texture_cache::sampled_image_descriptor*>(fs_sampler_state[i].get());
		const auto& tex = rsx::method_registers.fragment_textures[i];
		const auto previous_format_class = fs_sampler_state[i]->format_class;

		if (!m_samplers_dirty &&
			!m_textures_dirty[i] &&
			check_surface_cache_sampler_valid(sampler_state, tex))
		{
			continue;
		}

		const bool is_sampler_dirty = m_textures_dirty[i];
		m_textures_dirty[i] = false;

		if (!tex.enabled())
		{
			*sampler_state = {};
			continue;
		}

		*sampler_state = m_texture_cache.upload_texture(*m_current_command_buffer, tex, m_rtts);
		if (!sampler_state->validate())
		{
			continue;
		}

		if (sampler_state->is_cyclic_reference)
		{
			check_for_cyclic_refs |= true;
		}

		if (!is_sampler_dirty)
		{
			if (sampler_state->format_class != previous_format_class)
			{
				// Host details changed but RSX is not aware
				m_graphics_state |= rsx::fragment_program_state_dirty;
			}

			if (sampler_state->format_ex)
			{
				// Nothing to change, use cached sampler
				continue;
			}
		}

		sampler_state->format_ex = tex.format_ex();

		if (sampler_state->format_ex.texel_remap_control &&
			sampler_state->image_handle &&
			sampler_state->upload_context == rsx::texture_upload_context::shader_read &&
			(current_fp_metadata.bx2_texture_reads_mask & (1u << i)) == 0 &&
			!g_cfg.video.disable_hardware_texel_remapping) [[ unlikely ]]
		{
			// Check if we need to override the view format
			const auto vk_format = sampler_state->image_handle->format();
			VkFormat format_override = vk_format;;
			rsx::flags32_t flags_to_erase = 0u;
			rsx::flags32_t host_flags_to_set = 0u;

			if (sampler_state->format_ex.hw_SNORM_possible())
			{
				format_override = vk::get_compatible_snorm_format(vk_format);
				flags_to_erase = rsx::texture_control_bits::SEXT_MASK;
				host_flags_to_set = rsx::RSX_HOST_FORMAT_FEATURE_SNORM;
			}
			else if (sampler_state->format_ex.hw_SRGB_possible())
			{
				format_override = vk::get_compatible_srgb_format(vk_format);
				flags_to_erase = rsx::texture_control_bits::GAMMA_CTRL_MASK;
				host_flags_to_set = rsx::RSX_HOST_FORMAT_FEATURE_SRGB;
			}

			if (format_override != VK_FORMAT_UNDEFINED && format_override != vk_format)
			{
				sampler_state->image_handle = sampler_state->image_handle->as(format_override);
				sampler_state->format_ex.texel_remap_control &= (~flags_to_erase);
				sampler_state->format_ex.host_features |= host_flags_to_set;
			}
		}

		VkFilter mag_filter;
		vk::minification_filter min_filter;
		f32 min_lod = 0.f, max_lod = 0.f;
		f32 lod_bias = 0.f;

		const u32 texture_format = sampler_state->format_ex.format();
		VkBool32 compare_enabled = VK_FALSE;
		VkCompareOp depth_compare_mode = VK_COMPARE_OP_NEVER;

		if (texture_format >= CELL_GCM_TEXTURE_DEPTH24_D8 && texture_format <= CELL_GCM_TEXTURE_DEPTH16_FLOAT)
		{
			compare_enabled = VK_TRUE;
			depth_compare_mode = vk::get_compare_func(tex.zfunc(), true);
		}

		const f32 af_level = vk::max_aniso(tex.max_aniso());
		const auto wrap_s = vk::vk_wrap_mode(tex.wrap_s());
		const auto wrap_t = vk::vk_wrap_mode(tex.wrap_t());
		const auto wrap_r = vk::vk_wrap_mode(tex.wrap_r());

		// NOTE: In vulkan, the border color can bypass the sample swizzle stage.
		// Check the device properties to determine whether to pre-swizzle the colors or not.
		const bool sext_conv_required = (sampler_state->format_ex.texel_remap_control & rsx::texture_control_bits::SEXT_MASK) != 0;
		vk::border_color_t border_color(VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK);

		if (rsx::is_border_clamped_texture(tex))
		{
			auto color_value = get_border_color(tex, sext_conv_required);
			if (sampler_state->format_ex.host_snorm_format_active())
			{
				// Convert the border color in host space (2N - 1)
				// HW does the conversion in integer space as (x - 128) / 127 which introduces a biasing error.
				const float bias_v = 128.f / 255.f;
				const float scale_v = 255.f / 127.f;

				color4f scale{ 1.f }, bias{ 0.f };
				const auto snorm_mask = tex.argb_signed();
				if (snorm_mask & 1) { scale.a = scale_v; bias.a = -bias_v; }
				if (snorm_mask & 2) { scale.r = scale_v; bias.r = -bias_v; }
				if (snorm_mask & 4) { scale.g = scale_v; bias.g = -bias_v; }
				if (snorm_mask & 8) { scale.b = scale_v; bias.b = -bias_v; }
				color_value = (color_value + bias) * scale;
			}

			border_color = color_value;
		}

		// Check if non-point filtering can even be used on this format
		bool can_sample_linear;
		if (sampler_state->format_class == RSX_FORMAT_CLASS_COLOR) [[likely]]
		{
			// Most PS3-like formats can be linearly filtered without problem
			// Exclude textures that require SNORM conversion however
			can_sample_linear = !sext_conv_required;
		}
		else if (sampler_state->format_class != rsx::classify_format(texture_format) &&
			(texture_format == CELL_GCM_TEXTURE_A8R8G8B8 || texture_format == CELL_GCM_TEXTURE_D8R8G8B8))
		{
			// Depth format redirected to BGRA8 resample stage. Do not filter to avoid bits leaking
			can_sample_linear = false;
		}
		else
		{
			// Not all GPUs support linear filtering of depth formats
			const auto vk_format = sampler_state->image_handle ? sampler_state->image_handle->image()->format() :
				vk::get_compatible_sampler_format(m_device->get_formats_support(), sampler_state->external_subresource_desc.gcm_format);

			can_sample_linear = m_device->get_format_properties(vk_format).optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
		}

		const auto mipmap_count = tex.get_exact_mipmap_count();
		min_filter = vk::get_min_filter(tex.min_filter());

		if (can_sample_linear)
		{
			mag_filter = vk::get_mag_filter(tex.mag_filter());
		}
		else
		{
			mag_filter = VK_FILTER_NEAREST;
			min_filter.filter = VK_FILTER_NEAREST;
			min_filter.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		}

		if (min_filter.sample_mipmaps && mipmap_count > 1)
		{
			f32 actual_mipmaps;
			if (sampler_state->upload_context == rsx::texture_upload_context::shader_read)
			{
				actual_mipmaps = static_cast<f32>(mipmap_count);
			}
			else if (sampler_state->external_subresource_desc.op != rsx::deferred_request_command::nop)
			{
				actual_mipmaps = sampler_state->external_subresource_desc.exact_mip_count();
			}
			else
			{
				actual_mipmaps = 1.f;
			}

			if (actual_mipmaps > 1.f)
			{
				min_lod = tex.min_lod();
				max_lod = tex.max_lod();
				lod_bias = tex.bias();

				min_lod = std::min(min_lod, actual_mipmaps - 1.f);
				max_lod = std::min(max_lod, actual_mipmaps - 1.f);

				if (min_filter.mipmap_mode == VK_SAMPLER_MIPMAP_MODE_NEAREST)
				{
					// Round to nearest 0.5 to work around some broken games
					// Unlike openGL, sampler parameters cannot be dynamically changed on vulkan, leading to many permutations
					lod_bias = std::floor(lod_bias * 2.f + 0.5f) * 0.5f;
				}
			}
			else
			{
				min_lod = max_lod = lod_bias = 0.f;
				min_filter.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			}
		}

		if (fs_sampler_handles[i] &&
			fs_sampler_handles[i]->matches(wrap_s, wrap_t, wrap_r, false, lod_bias, af_level, min_lod, max_lod,
				min_filter.filter, mag_filter, min_filter.mipmap_mode, border_color, compare_enabled, depth_compare_mode))
		{
			continue;
		}

		fs_sampler_handles[i] = vk::get_resource_manager()->get_sampler(
			*m_device,
			fs_sampler_handles[i],
			wrap_s, wrap_t, wrap_r,
			false,
			lod_bias, af_level, min_lod, max_lod,
			min_filter.filter, mag_filter, min_filter.mipmap_mode,
			border_color, compare_enabled, depth_compare_mode);
	}

	for (u32 textures_ref = current_vp_metadata.referenced_textures_mask, i = 0; textures_ref; textures_ref >>= 1, ++i)
	{
		if (!(textures_ref & 1))
		{
			continue;
		}

		if (!vs_sampler_state[i])
		{
			vs_sampler_state[i] = std::make_unique<vk::texture_cache::sampled_image_descriptor>();
		}

		auto sampler_state = static_cast<vk::texture_cache::sampled_image_descriptor*>(vs_sampler_state[i].get());
		const auto& tex = rsx::method_registers.vertex_textures[i];
		const auto previous_format_class = sampler_state->format_class;

		if (!m_samplers_dirty &&
			!m_vertex_textures_dirty[i] &&
			check_surface_cache_sampler_valid(sampler_state, tex))
		{
			continue;
		}

		const bool is_sampler_dirty = m_vertex_textures_dirty[i];
		m_vertex_textures_dirty[i] = false;

		if (!rsx::method_registers.vertex_textures[i].enabled())
		{
			*sampler_state = {};
			continue;
		}

		*sampler_state = m_texture_cache.upload_texture(*m_current_command_buffer, tex, m_rtts);
		if (!sampler_state->validate())
		{
			continue;
		}

		if (sampler_state->is_cyclic_reference || sampler_state->external_subresource_desc.do_not_cache)
		{
			check_for_cyclic_refs |= true;
		}

		if (!is_sampler_dirty)
		{
			if (sampler_state->format_class != previous_format_class)
			{
				// Host details changed but RSX is not aware
				m_graphics_state |= rsx::vertex_program_state_dirty;
			}

			if (vs_sampler_handles[i])
			{
				continue;
			}
		}

		const VkBool32 unnormalized_coords = !!(tex.format() & CELL_GCM_TEXTURE_UN);
		const auto min_lod = tex.min_lod();
		const auto max_lod = tex.max_lod();
		const auto wrap_s = vk::vk_wrap_mode(tex.wrap_s());
		const auto wrap_t = vk::vk_wrap_mode(tex.wrap_t());

		// NOTE: In vulkan, the border color can bypass the sample swizzle stage.
		// Check the device properties to determine whether to pre-swizzle the colors or not.
		const auto border_color = is_border_clamped_texture(tex)
			? vk::border_color_t(get_border_color(tex, false))
			: vk::border_color_t(VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK);

		if (vs_sampler_handles[i] &&
			vs_sampler_handles[i]->matches(wrap_s, wrap_t, VK_SAMPLER_ADDRESS_MODE_REPEAT,
				unnormalized_coords, 0.f, 1.f, min_lod, max_lod, VK_FILTER_NEAREST, VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST, border_color))
		{
			continue;
		}

		vs_sampler_handles[i] = vk::get_resource_manager()->get_sampler(
			*m_device,
			vs_sampler_handles[i],
			wrap_s, wrap_t, VK_SAMPLER_ADDRESS_MODE_REPEAT,
			unnormalized_coords,
			0.f, 1.f, min_lod, max_lod,
			VK_FILTER_NEAREST, VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST, border_color);
	}

	m_samplers_dirty.store(false);

	if (current_fragment_program.ctrl & RSX_SHADER_CONTROL_EMULATE_DEPTH_COMPARE)
	{
		// Transition our FBO to a loop-friendly format.
		// We can also convert it into an input attachment, but for now this is easier.
		auto ds = ensure(m_rtts.m_bound_depth_stencil.second, "Invalid FS export configuration.");
		ds->texture_barrier(*m_current_command_buffer);

		check_for_cyclic_refs = true;
	}

	if (check_for_cyclic_refs)
	{
		// Regenerate renderpass key
		invalidate_render_pass();
	}

	if (backend_config.supports_asynchronous_compute)
	{
		// We have to do this here, because we have to assume the CB will be dumped
		auto async_task_scheduler = g_fxo->try_get<vk::AsyncTaskScheduler>();

		if (async_task_scheduler &&
			async_task_scheduler->is_recording() &&
			!async_task_scheduler->is_host_mode())
		{
			// Sync any async scheduler tasks
			if (auto ev = async_task_scheduler->get_primary_sync_label())
			{
				ev->gpu_wait(*m_current_command_buffer, m_async_compute_dependency_info);
			}
		}
	}
}

bool VKGSRender::bind_texture_env()
{
	bool out_of_memory = false;

	for (u32 textures_ref = current_fp_metadata.referenced_textures_mask, i = 0; textures_ref; textures_ref >>= 1, ++i)
	{
		if (!(textures_ref & 1))
		{
			// Unused TIU
			continue;
		}

		if (m_fs_binding_table->ftex_location[i] == umax)
		{
			// Corrupt shader table
			break;
		}

		vk::image_view* view = nullptr;
		auto sampler_state = static_cast<vk::texture_cache::sampled_image_descriptor*>(fs_sampler_state[i].get());

		if (rsx::method_registers.fragment_textures[i].enabled() &&
			sampler_state->validate())
		{
			if (view = sampler_state->image_handle; !view)
			{
				//Requires update, copy subresource
				if (!(view = m_texture_cache.create_temporary_subresource(*m_current_command_buffer, sampler_state->external_subresource_desc)))
				{
					out_of_memory = true;
				}
			}
			else
			{
				validate_image_layout_for_read_access(*m_current_command_buffer, view, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, sampler_state);
			}
		}

		if (view) [[likely]]
		{
			record_infamous2_capture_vk_texture(i, view, "recompiler", !sampler_state->image_handle, nullptr, fs_sampler_handles[i]);
			m_program->bind_uniform({ *view, *fs_sampler_handles[i] },
				vk::glsl::binding_set_index_fragment,
				m_fs_binding_table->ftex_location[i]);

			if (current_fragment_program.texture_state.redirected_textures & (1 << i))
			{
				// Stencil mirror required
				auto root_image = static_cast<vk::viewable_image*>(view->image());
				auto stencil_view = root_image->get_view(rsx::default_remap_vector, VK_IMAGE_ASPECT_STENCIL_BIT);

				if (!m_stencil_mirror_sampler)
				{
					m_stencil_mirror_sampler = std::make_unique<vk::sampler>(*m_device,
						VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
						VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
						VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
						VK_FALSE, 0.f, 1.f, 0.f, 0.f,
						VK_FILTER_NEAREST, VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST,
						VK_BORDER_COLOR_INT_OPAQUE_BLACK);
				}

				record_infamous2_capture_vk_texture(i, stencil_view, "recompiler_stencil", false, view, m_stencil_mirror_sampler.get());
				m_program->bind_uniform({ *stencil_view, *m_stencil_mirror_sampler },
					vk::glsl::binding_set_index_fragment,
					m_fs_binding_table->ftex_stencil_location[i]);
			}
		}
		else
		{
			const VkImageViewType view_type = vk::get_view_type(current_fragment_program.get_texture_dimension(i));
			const auto fallback_view = vk::null_image_view(*m_current_command_buffer, view_type);
			const VkDescriptorImageInfoEx desc = { *fallback_view, vk::null_sampler() };
			record_infamous2_capture_vk_texture(i, fallback_view, "recompiler_fallback", false);
			m_program->bind_uniform(desc,
				vk::glsl::binding_set_index_fragment,
				m_fs_binding_table->ftex_location[i]);

			if (current_fragment_program.texture_state.redirected_textures & (1 << i))
			{
				m_program->bind_uniform(desc,
					vk::glsl::binding_set_index_fragment,
					m_fs_binding_table->ftex_stencil_location[i]);
			}
		}
	}

	for (u32 textures_ref = current_vp_metadata.referenced_textures_mask, i = 0; textures_ref; textures_ref >>= 1, ++i)
	{
		if (!(textures_ref & 1))
		{
			// Unused TIU
			continue;
		}

		if (m_vs_binding_table->vtex_location[i] == umax)
		{
			// Corrupt shader
			break;
		}

		if (!rsx::method_registers.vertex_textures[i].enabled())
		{
			const auto view_type = vk::get_view_type(current_vertex_program.get_texture_dimension(i));
			m_program->bind_uniform({ *vk::null_image_view(*m_current_command_buffer, view_type), vk::null_sampler() },
				vk::glsl::binding_set_index_vertex,
				m_vs_binding_table->vtex_location[i]);

			continue;
		}

		auto sampler_state = static_cast<vk::texture_cache::sampled_image_descriptor*>(vs_sampler_state[i].get());
		auto image_ptr = sampler_state->image_handle;

		if (!image_ptr && sampler_state->validate())
		{
			if (!(image_ptr = m_texture_cache.create_temporary_subresource(*m_current_command_buffer, sampler_state->external_subresource_desc)))
			{
				out_of_memory = true;
			}
		}

		if (!image_ptr)
		{
			rsx_log.error("Texture upload failed to vtexture index %d. Binding null sampler.", i);
			const auto view_type = vk::get_view_type(current_vertex_program.get_texture_dimension(i));

			m_program->bind_uniform({ *vk::null_image_view(*m_current_command_buffer, view_type), vk::null_sampler() },
				vk::glsl::binding_set_index_vertex,
				m_vs_binding_table->vtex_location[i]);

			continue;
		}

		validate_image_layout_for_read_access(*m_current_command_buffer, image_ptr, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, sampler_state);

		m_program->bind_uniform({ *image_ptr, *vs_sampler_handles[i] },
			vk::glsl::binding_set_index_vertex,
			m_vs_binding_table->vtex_location[i]);
	}

	if (current_fragment_program.ctrl & RSX_SHADER_CONTROL_EMULATE_DEPTH_COMPARE)
	{
		auto ds = ensure(m_rtts.m_bound_depth_stencil.second);
		auto view = ds->get_view(rsx::default_remap_vector, VK_IMAGE_ASPECT_DEPTH_BIT);
		m_program->bind_uniform({ *view, vk::null_sampler() }, vk::glsl::binding_set_index_fragment, m_fs_binding_table->frag_depth_input_location);
	}

	if (current_fragment_program.ctrl & RSX_SHADER_CONTROL_PROGRAMMABLE_BLENDING)
	{
		ensure(current_fragment_program.mrt_buffers_count == m_draw_buffers.size());
		const auto remap = rsx::default_remap_vector.with_encoding(vk::VK_REMAP_IDENTITY);

		for (u32 i = 0; i < current_fragment_program.mrt_buffers_count; ++i)
		{
			auto viewable = static_cast<vk::viewable_image*>(m_fbo_images[i]);
			if (viewable->current_layout != VK_IMAGE_LAYOUT_GENERAL &&
				viewable->current_layout != VK_IMAGE_LAYOUT_ATTACHMENT_FEEDBACK_LOOP_OPTIMAL_EXT)
			{
				// Renderpass key is derived from the current layout, regenerate it after the transition
				vk::as_rtt(viewable)->texture_barrier(*m_current_command_buffer);
				invalidate_render_pass();
			}

			const auto view = viewable->get_view(remap);
			m_program->bind_uniform(*view, vk::glsl::binding_set_index_fragment, m_fs_binding_table->frag_src_location[i]);
		}
	}

	return out_of_memory;
}

bool VKGSRender::bind_interpreter_texture_env()
{
	if (current_fp_metadata.referenced_textures_mask == 0)
	{
		// Nothing to do
		return false;
	}

	std::array<VkDescriptorImageInfoEx, 68> texture_env;
	std::array<vk::image_view*, 4> fallback_views{};
	fallback_views[0] = vk::null_image_view(*m_current_command_buffer, VK_IMAGE_VIEW_TYPE_1D);
	VkDescriptorImageInfoEx fallback =
	{
		*fallback_views[0],
		vk::null_sampler()
	};

	auto start = texture_env.begin();
	auto end = start;

	// Fill default values
	// 1D
	std::advance(end, 16);
	std::fill(start, end, fallback);
	// 2D
	start = end;
	fallback_views[1] = vk::null_image_view(*m_current_command_buffer, VK_IMAGE_VIEW_TYPE_2D);
	fallback.imageView = fallback_views[1]->value;
	std::advance(end, 16);
	std::fill(start, end, fallback);
	// 3D
	start = end;
	fallback_views[3] = vk::null_image_view(*m_current_command_buffer, VK_IMAGE_VIEW_TYPE_3D);
	fallback.imageView = fallback_views[3]->value;
	std::advance(end, 16);
	std::fill(start, end, fallback);
	// CUBE
	start = end;
	fallback_views[2] = vk::null_image_view(*m_current_command_buffer, VK_IMAGE_VIEW_TYPE_CUBE);
	fallback.imageView = fallback_views[2]->value;
	std::advance(end, 16);
	std::fill(start, end, fallback);

	bool out_of_memory = false;

	auto decay_view_for_interpreter = [&](
		const rsx::image_section_attributes_t& attr,
		vk::texture_cache::sampled_image_descriptor* desc,
		vk::image_view* base,
		const rsx::texture_channel_remap_t& decoded_remap,
		bool is_msaa,
		bool is_redirected) -> vk::image_view*
	{
		if (!is_msaa && !is_redirected)
		{
			return base;
		}

		if (is_redirected && desc->image_type > rsx::texture_dimension_extended::texture_dimension_2d)
		{
			// Cannot handle redirect on 3D or cubemap with the interpreter.
			auto view_type = vk::get_view_type(desc->image_type);
			return vk::null_image_view(*m_current_command_buffer, view_type);
		}

		using deferred_subresource_t = vk::texture_cache::deferred_subresource;
		auto image = static_cast<vk::viewable_image*>(base->image());
		auto rtt = vk::try_as_rtt(base->image());

		if (is_msaa)
		{
			// MSAA resolve
			ensure(rtt);
			rtt->memory_barrier(*m_current_command_buffer, rsx::surface_access::transfer_read);
			image = rtt->get_surface(rsx::surface_access::transfer_read);
		}

		if (is_redirected)
		{
			// Force bitcast
			rsx::image_section_attributes_t flatten_attrs{};
			flatten_attrs.address = desc->ref_address;
			flatten_attrs.gcm_format = desc->format_ex.format();
			flatten_attrs.width = image->width();
			flatten_attrs.height = image->height();
			flatten_attrs.depth = 1;

			const coord3u flatten_rect = { 0, 0, 0, flatten_attrs.width, flatten_attrs.height, 1 };
			auto flatten_op = deferred_subresource_t::create_copy(
				image, flatten_attrs, flatten_rect, rsx::surface_transform::identity, decoded_remap, desc->is_cyclic_reference);

			ensure(desc->ref_address);
			flatten_op.cache_range = rtt
				? rtt->get_memory_range()
				: utils::address_range32::start_length(desc->ref_address, attr.pitch * attr.height);

			return m_texture_cache.create_temporary_subresource(*m_current_command_buffer, flatten_op);
		}

		image->change_layout(*m_current_command_buffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		return image->get_view(decoded_remap, base->info.subresourceRange.aspectMask);
	};

	for (u32 textures_ref = current_fp_metadata.referenced_textures_mask, i = 0; textures_ref; textures_ref >>= 1, ++i)
	{
		if (!(textures_ref & 1))
			continue;

		vk::image_view* view = nullptr;
		auto sampler_state = static_cast<vk::texture_cache::sampled_image_descriptor*>(fs_sampler_state[i].get());

		auto& tex = rsx::method_registers.fragment_textures[i];
		if (tex.enabled() && sampler_state->validate())
		{
			if (view = sampler_state->image_handle; !view)
			{
				//Requires update, copy subresource
				if (!(view = m_texture_cache.create_temporary_subresource(*m_current_command_buffer, sampler_state->external_subresource_desc)))
				{
					out_of_memory = true;
				}
			}
		}

		if (!view)
		{
			// OOM or disabled texture
			record_infamous2_capture_vk_texture(i, fallback_views[static_cast<u32>(current_fragment_program.get_texture_dimension(i))],
				"interpreter_fallback", false);
			continue;
		}

		auto primary_view = view;

		// Flatten MSAA and DEPTH24S8 redirects
		if (view->image()->samples() > 1 || view->info.subresourceRange.aspectMask != VK_IMAGE_ASPECT_COLOR_BIT)
		{
			const auto mask = (1u << i);
			const bool is_redirected = !!(current_fragment_program.texture_state.redirected_textures & mask);
			const bool is_msaa = !!(current_fragment_program.texture_state.multisampled_textures & mask);
			if (is_redirected || is_msaa)
			{
				view = decay_view_for_interpreter(
					tex.attributes(),
					sampler_state,
					view,
					tex.decoded_remap(),
					is_msaa,
					is_redirected);

				if (!view)
				{
					// OOM
					out_of_memory = true;
					record_infamous2_capture_vk_texture(i, fallback_views[static_cast<u32>(sampler_state->image_type)],
						"interpreter_decay_fallback", false, primary_view);
					continue;
				}
			}
		}

		if (primary_view == view)
		{
			validate_image_layout_for_read_access(*m_current_command_buffer, view, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, sampler_state);
		}

		const int offsets[] = { 0, 16, 48, 32 };
		auto& sampled_image_info = texture_env[offsets[static_cast<u32>(sampler_state->image_type)] + i];
		record_infamous2_capture_vk_texture(i, view, "interpreter", !sampler_state->image_handle, primary_view, fs_sampler_handles[i]);
		sampled_image_info = { *view, *fs_sampler_handles[i] };
	}

	m_shader_interpreter.update_fragment_textures(texture_env);
	return out_of_memory;
}

void VKGSRender::emit_geometry(u32 sub_index)
{
	auto &draw_call = rsx::method_registers.current_draw_clause;
	m_profiler.start();

	const rsx::flags32_t vertex_state_mask = rsx::vertex_base_changed | rsx::vertex_arrays_changed;
	const rsx::flags32_t state_flags = (sub_index == 0) ? rsx::vertex_arrays_changed : draw_call.execute_pipeline_dependencies(m_ctx);

	if (state_flags & rsx::vertex_arrays_changed)
	{
		m_draw_processor.analyse_inputs_interleaved(m_vertex_layout, current_vp_metadata);
	}
	else if (state_flags & rsx::vertex_base_changed)
	{
		// Rebase vertex bases instead of
		for (auto& info : m_vertex_layout.interleaved_blocks)
		{
			info->vertex_range.second = 0;
			const auto vertex_base_offset = rsx::method_registers.vertex_data_base_offset();
			info->real_offset_address = rsx::get_address(rsx::get_vertex_offset_from_base(vertex_base_offset, info->base_offset), info->memory_location);
		}
	}
	else
	{
		// Discard cached results
		for (auto& info : m_vertex_layout.interleaved_blocks)
		{
			info->vertex_range.second = 0;
		}
	}

	if ((state_flags & vertex_state_mask) && !m_vertex_layout.validate())
	{
		// No vertex inputs enabled
		if (m_infamous2_vk_candidate)
		{
			record_infamous2_capture_vk("skip", fmt::format("\"reason\":\"invalid_vertex_layout\",\"rsx_sub_index\":%u", sub_index));
		}
		// Execute remainining pipeline barriers with NOP draw
		do
		{
			draw_call.execute_pipeline_dependencies(m_ctx);
		}
		while (draw_call.next());

		draw_call.end();
		return;
	}

	// Programs data is dependent on vertex state
	auto upload_info = upload_vertex_data();
	if (!upload_info.vertex_draw_count)
	{
		// Malformed vertex setup; abort
		if (m_infamous2_vk_candidate)
		{
			record_infamous2_capture_vk("skip", fmt::format("\"reason\":\"zero_vertex_draw_count\",\"rsx_sub_index\":%u", sub_index));
		}
		return;
	}

	m_frame_stats.vertex_upload_time += m_profiler.duration();

	// Faults are allowed during vertex upload. Ensure consistent CB state after uploads.
	// Queries are spawned and closed outside render pass scope for consistency reasons.
	if (m_current_command_buffer->flags & vk::command_buffer::cb_load_occluson_task)
	{
		u32 occlusion_id = m_occlusion_query_manager->allocate_query(*m_current_command_buffer);
		if (occlusion_id == umax)
		{
			// Force flush
			rsx_log.warning("[Performance Warning] Out of free occlusion slots. Forcing hard sync.");
			ZCULL_control::sync(this);

			occlusion_id = m_occlusion_query_manager->allocate_query(*m_current_command_buffer);
			if (occlusion_id == umax)
			{
				//rsx_log.error("Occlusion pool overflow");
				if (m_current_task) m_current_task->result = 1;
			}
		}

		// Before starting a query, we need to match RP scope (VK_1_0 rules).
		// We always want our queries to start outside a renderpass whenever possible.
		// We ignore this for performance reasons whenever possible of course and only do this for sensitive drivers.
		if (vk::use_strict_query_scopes() &&
			vk::is_renderpass_open(*m_current_command_buffer))
		{
			vk::end_renderpass(*m_current_command_buffer);
			emergency_query_cleanup(m_current_command_buffer);
		}

		// Begin query
		m_occlusion_query_manager->begin_query(*m_current_command_buffer, occlusion_id);

		auto& data = m_occlusion_map[m_active_query_info->driver_handle];
		data.indices.push_back(occlusion_id);
		data.set_sync_command_buffer(m_current_command_buffer);

		m_current_command_buffer->flags &= ~vk::command_buffer::cb_load_occluson_task;
		m_current_command_buffer->flags |= (vk::command_buffer::cb_has_occlusion_task | vk::command_buffer::cb_has_open_query);
	}

	VkDescriptorBufferViewEx persistent_buffer = m_persistent_attribute_storage ? *m_persistent_attribute_storage : *null_buffer_view;
	VkDescriptorBufferViewEx volatile_buffer = m_volatile_attribute_storage ? *m_volatile_attribute_storage : *null_buffer_view;
	bool update_descriptors = false;

	if (m_current_draw.subdraw_id == 0)
	{
		update_descriptors = true;

		// Allocate stream layout memory for this batch
		const u64 alloc_size = rsx::method_registers.current_draw_clause.pass_count() * 168;
		m_vertex_layout_dynamic_offset = m_vertex_layout_ring_info.alloc<8>(alloc_size);
	}

	// Update vertex fetch parameters
	update_vertex_env(sub_index, upload_info);

	if (update_descriptors)
	{
		m_program->bind_uniform(persistent_buffer, vk::glsl::binding_set_index_vertex, m_vs_binding_table->vertex_buffers_location);
		m_program->bind_uniform(volatile_buffer, vk::glsl::binding_set_index_vertex, m_vs_binding_table->vertex_buffers_location + 1);
	}

	bool reload_state = (!m_current_draw.subdraw_id++);
	vk::renderpass_op(*m_current_command_buffer, [&](const vk::command_buffer& cmd, VkRenderPass pass, VkFramebuffer fbo)
	{
		if (get_render_pass() == pass && m_draw_fbo->value == fbo)
		{
			// Nothing to do
			return;
		}

		if (pass)
		{
			// Subpass mismatch, end it before proceeding
			vk::end_renderpass(cmd);
		}

		// Starting a new renderpass should clobber dynamic state
		m_current_command_buffer->flags |= vk::command_buffer::cb_reload_dynamic_state;

		reload_state = true;
	});

	if (current_fragment_program.ctrl & RSX_SHADER_CONTROL_PROGRAMMABLE_BLENDING)
	{
		// Subpass inter-draw dependency for input attachment reads. Preserves open renderpasses.
		for (u32 i = 0; i < current_fragment_program.mrt_buffers_count; ++i)
		{
			vk::insert_image_memory_barrier(
				*m_current_command_buffer,
				m_fbo_images[i]->value,
				m_fbo_images[i]->current_layout,
				m_fbo_images[i]->current_layout,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
				VK_ACCESS_INPUT_ATTACHMENT_READ_BIT,
				{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
				true,
				VK_DEPENDENCY_BY_REGION_BIT
			);
		}
	}

	// Bind both pipe and descriptors in one go
	// FIXME: We only need to rebind the pipeline when reload state is set. Flags?
	m_program->bind(*m_current_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS);

	if (reload_state)
	{
		update_draw_state();
		begin_render_pass();

		if (cond_render_ctrl.hw_cond_active && m_device->get_conditional_render_support())
		{
			// It is inconvenient that conditional rendering breaks other things like compute dispatch
			// TODO: If this is heavy, add refactor the resources into global and add checks around compute dispatch
			VkConditionalRenderingBeginInfoEXT info{};
			info.sType = VK_STRUCTURE_TYPE_CONDITIONAL_RENDERING_BEGIN_INFO_EXT;
			info.buffer = m_cond_render_buffer->value;

			_vkCmdBeginConditionalRenderingEXT(*m_current_command_buffer, &info);
			m_current_command_buffer->flags |= vk::command_buffer::cb_has_conditional_render;
		}
	}

	// Bind the new set of descriptors for use with this draw call
	m_frame_stats.setup_time += m_profiler.duration();
	if (m_infamous2_vk_candidate) m_infamous2_vk_emitted_groups++;

	if (!upload_info.index_info)
	{
		if (draw_call.is_trivial_instanced_draw)
		{
			if (m_infamous2_vk_candidate) record_infamous2_capture_vk_draw(sub_index, upload_info.vertex_draw_count, false, draw_call.pass_count(), 0, 1, "vkCmdDraw");
			vkCmdDraw(*m_current_command_buffer, upload_info.vertex_draw_count, draw_call.pass_count(), 0, 0);
		}
		else if (draw_call.is_single_draw())
		{
			if (m_infamous2_vk_candidate) record_infamous2_capture_vk_draw(sub_index, upload_info.vertex_draw_count, false, 1, 0, 1, "vkCmdDraw");
			vkCmdDraw(*m_current_command_buffer, upload_info.vertex_draw_count, 1, 0, 0);
		}
		else if (m_device->get_multidraw_support())
		{
			const auto& subranges = draw_call.get_subranges();
			auto ptr = utils::bless<const VkMultiDrawInfoEXT>(&subranges.front().first);
			if (m_infamous2_vk_candidate) record_infamous2_capture_vk_draw(sub_index, upload_info.vertex_draw_count, false, 1, 0, ::size32(subranges), "vkCmdDrawMultiEXT");
			_vkCmdDrawMultiEXT(*m_current_command_buffer, ::size32(subranges), ptr, 1, 0, sizeof(rsx::draw_range_t));
		}
		else
		{
			u32 vertex_offset = 0;
			const auto& subranges = draw_call.get_subranges();
			for (const auto& range : subranges)
			{
				if (m_infamous2_vk_candidate) record_infamous2_capture_vk_draw(sub_index, range.count, false, 1, vertex_offset, 1, "vkCmdDraw");
				vkCmdDraw(*m_current_command_buffer, range.count, 1, vertex_offset, 0);
				vertex_offset += range.count;
			}
		}
	}
	else
	{
		const VkIndexType index_type = std::get<1>(*upload_info.index_info);
		const VkDeviceSize offset = std::get<0>(*upload_info.index_info);

		vkCmdBindIndexBuffer(*m_current_command_buffer, m_index_buffer_ring_info.heap->value, offset, index_type);

		if (draw_call.is_trivial_instanced_draw)
		{
			if (m_infamous2_vk_candidate) record_infamous2_capture_vk_draw(sub_index, upload_info.vertex_draw_count, true, draw_call.pass_count(), 0, 1, "vkCmdDrawIndexed");
			vkCmdDrawIndexed(*m_current_command_buffer, upload_info.vertex_draw_count, draw_call.pass_count(), 0, 0, 0);
		}
		else if (rsx::method_registers.current_draw_clause.is_single_draw())
		{
			if (m_infamous2_vk_candidate) record_infamous2_capture_vk_draw(sub_index, upload_info.vertex_draw_count, true, 1, 0, 1, "vkCmdDrawIndexed");
			vkCmdDrawIndexed(*m_current_command_buffer, upload_info.vertex_draw_count, 1, 0, 0, 0);
		}
		else if (m_device->get_multidraw_support())
		{
			const auto& subranges = draw_call.get_subranges();
			const auto subranges_count = ::size32(subranges);
			const auto allocation_size = subranges_count * sizeof(VkMultiDrawIndexedInfoEXT);

			m_multidraw_parameters_buffer.resize(allocation_size);
			auto base_ptr = utils::bless<VkMultiDrawIndexedInfoEXT>(m_multidraw_parameters_buffer.data());

			u32 vertex_offset = 0;
			auto _ptr = base_ptr;

			for (const auto& range : subranges)
			{
				const auto count = get_index_count(draw_call.primitive, range.count);
				_ptr->firstIndex = vertex_offset;
				_ptr->indexCount = count;
				_ptr->vertexOffset = 0;

				_ptr++;
				vertex_offset += count;
			}
			if (m_infamous2_vk_candidate) record_infamous2_capture_vk_draw(sub_index, upload_info.vertex_draw_count, true, 1, 0, subranges_count, "vkCmdDrawMultiIndexedEXT");
			_vkCmdDrawMultiIndexedEXT(*m_current_command_buffer, subranges_count, base_ptr, 1, 0, sizeof(VkMultiDrawIndexedInfoEXT), nullptr);
		}
		else
		{
			u32 vertex_offset = 0;
			const auto& subranges = draw_call.get_subranges();
			for (const auto& range : subranges)
			{
				const auto count = get_index_count(draw_call.primitive, range.count);
				if (m_infamous2_vk_candidate) record_infamous2_capture_vk_draw(sub_index, count, true, 1, vertex_offset, 1, "vkCmdDrawIndexed");
				vkCmdDrawIndexed(*m_current_command_buffer, count, 1, vertex_offset, 0, 0);
				vertex_offset += count;
			}
		}
	}

	m_frame_stats.draw_exec_time += m_profiler.duration();
}

void VKGSRender::begin()
{
	// Save shader state now before prefetch and loading happens
	m_interpreter_state = (m_graphics_state.load() & rsx::pipeline_state::invalidate_pipeline_bits);

	rsx::thread::begin();

	if (skip_current_frame ||
		cond_render_ctrl.disable_rendering())
	{
		return;
	}

	init_buffers(rsx::framebuffer_creation_context::context_draw);

	if (m_graphics_state & rsx::pipeline_state::invalidate_pipeline_bits)
	{
		// Shaders need to be reloaded.
		m_prev_program = m_program;
		m_program = nullptr;
	}
}

void VKGSRender::end()
{
	m_infamous2_draws_since_submit++;
	m_infamous2_vk_candidate = infamous2_capture_vk_candidate();
	if (m_infamous2_vk_candidate)
	{
		m_infamous2_vk_binding_attempt = 0;
		m_infamous2_vk_emitted_groups = 0;
		m_infamous2_vk_emitted_commands = 0;
		const auto& r = rsx::method_registers;
		const auto [fp_offset, fp_location] = r.shader_program_address();
		record_infamous2_capture_vk("batch_enter", fmt::format("\"filter\":\"state_candidate_v1\",\"surface_clip\":[%u,%u,%u,%u],"
			"\"blend_mask\":%u,\"depth_test\":%u,\"depth_write\":%u,\"fp_offset\":%u,\"fp_location\":%u,"
			"\"primitive\":%u,\"draw_command\":%u,\"program_state_is_current\":0,\"correlation\":\"backend_clause_hint\"",
			r.surface_clip_origin_x(), r.surface_clip_origin_y(), r.surface_clip_width(), r.surface_clip_height(), r.blend_enabled_mask(),
			static_cast<u32>(r.depth_test_enabled()), static_cast<u32>(r.depth_write_enabled()), fp_offset, fp_location,
			static_cast<u32>(r.current_draw_clause.primitive), static_cast<u32>(r.current_draw_clause.command)));
	}
	if (should_skip_draw())
	{
		if (m_infamous2_vk_candidate)
		{
			record_infamous2_capture_vk("skip", fmt::format("\"reason\":\"should_skip_draw\",\"skip_frame\":%u,\"rtt_valid\":%u,\"no_attachments\":%u,\"conditional_disabled\":%u",
				static_cast<u32>(skip_current_frame), static_cast<u32>(m_graphics_state.test(rsx::rtt_config_valid)),
				static_cast<u32>(m_graphics_state.test(rsx::rtt_config_no_attachments)), static_cast<u32>(cond_render_ctrl.disable_rendering())));
		}
		execute_nop_draw();
		rsx::thread::end();
		return;
	}

	m_profiler.start();

	// Check for frame resource status here because it is possible for an async flip to happen between begin/end
	if (m_current_frame->flags & frame_context_state::dirty) [[unlikely]]
	{
		check_present_status();

		if (m_current_frame->swap_command_buffer) [[unlikely]]
		{
			// Borrow time by using the auxilliary context
			m_aux_frame_context.grab_resources(*m_current_frame);
			m_current_frame = &m_aux_frame_context;
		}

		ensure(!m_current_frame->swap_command_buffer);

		m_current_frame->flags &= ~frame_context_state::dirty;
	}

	analyse_current_rsx_pipeline();

	m_frame_stats.setup_time += m_profiler.duration();

	load_texture_env();
	m_frame_stats.textures_upload_time += m_profiler.duration();

	if (!load_program())
	{
		// Program is not ready, skip drawing this
		if (m_infamous2_vk_candidate)
		{
			record_infamous2_capture_vk("skip", "\"reason\":\"program_not_ready\"");
		}
		std::this_thread::yield();
		execute_nop_draw();
		// m_rtts.on_write(); - breaks games for obvious reasons
		rsx::thread::end();
		return;
	}

	// Load program execution environment
	load_program_env();
	m_frame_stats.setup_time += m_profiler.duration();

	// Apply write memory barriers
	if (auto ds = std::get<1>(m_rtts.m_bound_depth_stencil))
	{
		ds->write_barrier(*m_current_command_buffer);

		if (m_graphics_state.test(rsx::zeta_address_cyclic_barrier) &&
			ds->current_layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
		{
			// We actually need to end the subpass as a minimum. Without this, early-Z optimiazations in following draws will clobber reads from previous draws and cause flickering.
			// Since we're ending the subpass, might as well restore DCC/HiZ for extra performance
			ds->change_layout(*m_current_command_buffer, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
			ds->reset_surface_counters();

			// Regenerate render pass key
			invalidate_render_pass();
		}
	}

	for (auto &rtt : m_rtts.m_bound_render_targets)
	{
		if (auto surface = std::get<1>(rtt))
		{
			surface->write_barrier(*m_current_command_buffer);
		}
	}

	m_graphics_state.clear(rsx::zeta_address_cyclic_barrier);

	m_frame_stats.setup_time += m_profiler.duration();

	// Now bind the shader resources. It is important that this takes place after the barriers so that we don't end up with stale descriptors
	for (int retry = 0; retry < 3; ++retry)
	{
		if (m_infamous2_vk_candidate) m_infamous2_vk_binding_attempt = static_cast<u32>(retry);
		if (retry > 0 && m_samplers_dirty) [[ unlikely ]]
		{
			// Reload texture env if referenced objects were invalidated during OOM handling.
			load_texture_env();

			// Do not trust fragment/vertex texture state after a texture state reset.
			// NOTE: We don't want to change the program - it's too late for that now. We just need to harmonize the state.
			m_graphics_state |= rsx::vertex_program_state_dirty | rsx::fragment_program_state_dirty;
			get_current_fragment_program(fs_sampler_state);
			get_current_vertex_program(vs_sampler_state);
			m_graphics_state.clear(rsx::pipeline_state::invalidate_pipeline_bits);
		}

		const bool out_of_memory = m_shader_interpreter.is_interpreter(m_program)
			? bind_interpreter_texture_env()
			: bind_texture_env();

		if (!out_of_memory)
		{
			break;
		}
		if (m_infamous2_vk_candidate)
		{
			record_infamous2_capture_vk("texture_retry", fmt::format("\"binding_attempt\":%u,\"reason\":\"out_of_memory\"", m_infamous2_vk_binding_attempt));
		}

		// Handle OOM
		if (!on_vram_exhausted(rsx::problem_severity::fatal))
		{
			// It is not possible to free memory. Just use placeholder textures. Can cause graphics glitches but shouldn't crash otherwise
			break;
		}
	}

	m_texture_cache.release_uncached_temporary_subresources();
	m_frame_stats.textures_upload_time += m_profiler.duration();

	u32 sub_index = 0;               // RSX subdraw ID
	m_current_draw.subdraw_id = 0;   // Host subdraw ID. Invalid RSX subdraws do not increment this value

	if (m_graphics_state & rsx::pipeline_state::invalidate_vk_dynamic_state)
	{
		m_current_command_buffer->flags |= vk::command_buffer::cb_reload_dynamic_state;
	}

	auto& draw_call = rsx::method_registers.current_draw_clause;
	draw_call.begin();
	do
	{
		emit_geometry(sub_index++);

		if (draw_call.is_trivial_instanced_draw)
		{
			// We already completed. End the draw.
			draw_call.end();
		}
	}
	while (draw_call.next());

	if (m_current_command_buffer->flags & vk::command_buffer::cb_has_conditional_render)
	{
		_vkCmdEndConditionalRenderingEXT(*m_current_command_buffer);
		m_current_command_buffer->flags &= ~(vk::command_buffer::cb_has_conditional_render);
	}

	m_rtts.on_write(m_framebuffer_layout.color_write_enabled, m_framebuffer_layout.zeta_write_enabled);
	if (m_infamous2_vk_candidate)
	{
		record_infamous2_capture_vk("batch_complete", fmt::format("\"emitted_geometry_groups\":%u,\"recorded_vk_commands\":%u,\"last_binding_attempt\":%u,"
			"\"gpu_execution_confirmed\":0,\"logical_merge_mapping_available\":0",
			m_infamous2_vk_emitted_groups, m_infamous2_vk_emitted_commands, m_infamous2_vk_binding_attempt));
	}

	rsx::thread::end();
}
