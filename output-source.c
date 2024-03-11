#include <obs-module.h>
#include <stdio.h>
#include <obs-frontend-api.h>

struct output_source_context {
	obs_source_t *source;
	bool rendering;
	char *view_name;
	uint32_t channel;
	obs_source_t *outputSource;
	uint32_t width;
	uint32_t height;
	struct vec4 color;
	bool recurring;
};

size_t get_view_count();
const char *get_view_name(size_t idx);
obs_view_t *get_view_by_name(const char *view_name);

static const char *output_source_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("OutputSource");
}

static void output_source_update(void *data, obs_data_t *settings)
{
	struct output_source_context *context = data;
	const char *view_name = obs_data_get_string(settings, "view");
	if (!context->view_name || strcmp(view_name, context->view_name) != 0) {
		bfree(context->view_name);
		context->view_name = bstrdup(view_name);
	}

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
	bfree(context->view_name);
	bfree(context);
}

#define channel_name_count 7

static char *channel_names[] = {"StudioMode.Program",   "Basic.DesktopDevice1",
				"Basic.DesktopDevice2", "Basic.AuxDevice1",
				"Basic.AuxDevice2",     "Basic.AuxDevice3",
				"Basic.AuxDevice4"};

static bool view_changed(void *priv, obs_properties_t *props,
			 obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(priv);
	UNUSED_PARAMETER(property);
	obs_property_t *channels = obs_properties_get(props, "channel");
	const char *view_name = obs_data_get_string(settings, "view");
	bool changed = false;
	char buffer[10];
	for (int i = 0; i < channel_name_count; i++) {
		if (strlen(view_name) && i > 0) {
			snprintf(buffer, 10, "%i", i);
			if (strcmp(buffer, obs_property_list_item_name(
						   channels, i)) != 0) {
				obs_property_list_item_remove(channels, i);
				obs_property_list_insert_int(channels, i,
							     buffer, i);
				changed = true;
			}
		} else {
			const char *name = obs_frontend_get_locale_string(
				channel_names[i]);
			if (strcmp(name, obs_property_list_item_name(channels,
								     i)) != 0) {
				obs_property_list_item_remove(channels, i);
				obs_property_list_insert_int(channels, i, name,
							     i);
				changed = true;
			}
		}
	}
	return changed;
}

static obs_properties_t *output_source_properties(void *data)
{
	obs_properties_t *ppts = obs_properties_create();

	size_t c = get_view_count();
	if (c > 1) {
		obs_property_t *p = obs_properties_add_list(
			ppts, "view", obs_module_text("View"),
			OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
		for (size_t i = 0; i < c; i++) {
			const char *name = get_view_name(i);
			obs_property_list_add_string(p, name, name);
		}
		obs_property_set_modified_callback2(p, view_changed, data);
	}
	obs_property_t *p = obs_properties_add_list(ppts, "channel",
						    obs_module_text("Channel"),
						    OBS_COMBO_TYPE_LIST,
						    OBS_COMBO_FORMAT_INT);
	char buffer[10];

	for (int i = 0; i < MAX_CHANNELS; i++) {
		if (i < channel_name_count) {
			obs_property_list_add_int(
				p,
				obs_frontend_get_locale_string(
					channel_names[i]),
				i);
		} else {
			snprintf(buffer, 10, "%i", i);
			obs_property_list_add_int(p, buffer, i);
		}
	}

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

	obs_view_t *view = NULL;
	if (strlen(context->view_name))
		view = get_view_by_name(context->view_name);

	obs_source_t *source =
		view ? obs_view_get_source(view, context->channel)
		     : obs_get_output_source(context->channel);
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
