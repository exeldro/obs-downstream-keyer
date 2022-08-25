#include <obs-module.h>

struct output_source_context {
	obs_source_t *source;
	bool rendering;
	uint32_t channel;
	obs_source_t *outputSource;
	uint32_t width;
	uint32_t height;
	struct vec4 color;
	bool recurring;
};

static const char *output_source_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("OutputSource");
}

static void output_source_update(void *data, obs_data_t *settings)
{
	struct output_source_context *context = data;
	context->channel = (uint32_t)obs_data_get_int(settings, "channel");
	vec4_from_rgba(&context->color,
		       (uint32_t)obs_data_get_int(settings, "color"));
}

static void *output_source_create(obs_data_t *settings, obs_source_t *source)
{
	struct output_source_context *context =
		bzalloc(sizeof(struct output_source_context));
	context->source = source;

	output_source_update(context, settings);
	return context;
}

static void output_source_destroy(void *data)
{
	struct output_source_context *context = data;

	bfree(context);
}

static obs_properties_t *output_source_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *ppts = obs_properties_create();
	obs_properties_add_int(ppts, "channel", obs_module_text("Channel"), 0,
			       MAX_CHANNELS - 1, 1);
	obs_properties_add_color(ppts, "color",
				 obs_module_text("FallbackColor"));
	return ppts;
}

void output_source_defaults(obs_data_t *settings)
{
	UNUSED_PARAMETER(settings);
}

static void output_source_video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct output_source_context *context = data;
	if (context->rendering || context->recurring ||
	    !context->outputSource) {
		gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
		gs_eparam_t *color =
			gs_effect_get_param_by_name(solid, "color");
		gs_technique_t *tech = gs_effect_get_technique(solid, "Solid");

		gs_effect_set_vec4(color, &context->color);

		gs_technique_begin(tech);
		gs_technique_begin_pass(tech, 0);

		gs_draw_sprite(0, 0, context->width, context->height);

		gs_technique_end_pass(tech);
		gs_technique_end(tech);
		return;
	}

	context->rendering = true;
	obs_source_video_render(context->outputSource);
	context->rendering = false;
}

static uint32_t output_source_getwidth(void *data)
{
	struct output_source_context *context = data;
	return context->width;
}

static uint32_t output_source_getheight(void *data)
{
	struct output_source_context *context = data;
	return context->height;
}

static void check_recursion(obs_source_t *parent, obs_source_t *child,
			    void *data)
{
	UNUSED_PARAMETER(parent);
	struct output_source_context *context = data;
	if (child == context->source) {
		context->recurring = true;
	}
}

static void output_source_video_tick(void *data, float seconds)
{
	UNUSED_PARAMETER(seconds);
	struct output_source_context *context = data;
	obs_source_t *source = obs_get_output_source(context->channel);
	if (!source) {
		if (context->outputSource) {
			context->outputSource = NULL;
			context->recurring = false;
		}
		return;
	}
	context->recurring = false;
	obs_source_enum_active_tree(source, check_recursion, data);
	context->outputSource = source;
	context->width = obs_source_get_width(source);
	context->height = obs_source_get_height(source);
	obs_source_release(source);
}

struct obs_source_info output_source_info = {
	.id = "ouput_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW,
	.get_name = output_source_get_name,
	.create = output_source_create,
	.destroy = output_source_destroy,
	.load = output_source_update,
	.update = output_source_update,
	.get_properties = output_source_properties,
	.get_defaults = output_source_defaults,
	.video_render = output_source_video_render,
	.video_tick = output_source_video_tick,
	.get_width = output_source_getwidth,
	.get_height = output_source_getheight,
	.icon_type = OBS_ICON_TYPE_UNKNOWN,
};
