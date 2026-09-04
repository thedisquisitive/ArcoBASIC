shader_type spatial;
render_mode blend_mix, cull_back, diffuse_burley, specular_schlick_ggx;

// Arco3D layered "smart material" -- the user's own explicit ask: "just a paint/apply image and
// slide it around on the model, add decals, use parts of a texture and slide that part onto the
// model instead of unwrapping it" (RFC-0050 Section 20's own Triplanar/Box/Planar/Decal mapping
// modes, none of which need a UV unwrap at all). Up to MAX_LAYERS layers, composited in order --
// a real, documented scope boundary (see Main.gd's own "Material layers" section for why 4, not
// unlimited).
//
// Every mapping mode shares ONE mechanism for "slide it around / resize / reorient": each layer's
// own Offset/Scale/RotationDegrees (authored in Main.gd) becomes a real Transform3D, and its
// INVERSE is passed here as layer_inverse_transform -- the object-space vertex position is
// transformed THROUGH that inverse before any projection math runs, so moving/scaling/rotating the
// layer's own transform is exactly equivalent to moving/scaling/rotating a real physical projector
// relative to the model. No per-mapping-mode-specific offset logic needed.

const int MAX_LAYERS = 4;
const int MAPPING_TRIPLANAR = 0;
const int MAPPING_PLANAR = 1;
const int MAPPING_BOX = 2;
const int MAPPING_DECAL = 3;
const int BLEND_NORMAL = 0;
const int BLEND_MULTIPLY = 1;
const int BLEND_ADD = 2;
const int BLEND_SCREEN = 3;

uniform vec4 base_color : source_color = vec4(0.6, 0.7, 0.85, 1.0);
uniform bool selection_highlight = false;
uniform vec4 selection_highlight_color : source_color = vec4(0.95, 0.8, 0.3, 1.0);

uniform sampler2D layer_textures[MAX_LAYERS] : source_color;
uniform bool layer_enabled[MAX_LAYERS];
uniform int layer_mapping[MAX_LAYERS];
uniform mat4 layer_inverse_transform[MAX_LAYERS];
uniform vec4 layer_source_rect[MAX_LAYERS]; // x, y, w, h in 0..1 source-image space
uniform float layer_opacity[MAX_LAYERS];
uniform int layer_blend[MAX_LAYERS];

varying vec3 object_position;
varying vec3 object_normal;

void vertex() {
	object_position = VERTEX;
	object_normal = NORMAL;
}

vec3 apply_blend(int mode, vec3 base, vec3 layer_color, float amount) {
	vec3 target = layer_color;
	if (mode == BLEND_MULTIPLY) {
		target = base * layer_color;
	} else if (mode == BLEND_ADD) {
		target = base + layer_color;
	} else if (mode == BLEND_SCREEN) {
		target = vec3(1.0) - (vec3(1.0) - base) * (vec3(1.0) - layer_color);
	}
	return mix(base, target, amount);
}

void fragment() {
	vec3 result = base_color.rgb;

	for (int i = 0; i < MAX_LAYERS; i++) {
		if (!layer_enabled[i]) {
			continue;
		}
		vec3 p = (layer_inverse_transform[i] * vec4(object_position, 1.0)).xyz;
		vec4 rect = layer_source_rect[i];
		vec4 sample_color = vec4(0.0);
		float coverage = 1.0;

		if (layer_mapping[i] == MAPPING_TRIPLANAR) {
			vec3 weights = abs(object_normal);
			weights = weights / max(weights.x + weights.y + weights.z, 0.0001);
			vec2 uv_x = rect.xy + fract(p.yz) * rect.zw;
			vec2 uv_y = rect.xy + fract(p.xz) * rect.zw;
			vec2 uv_z = rect.xy + fract(p.xy) * rect.zw;
			sample_color = texture(layer_textures[i], uv_x) * weights.x
				+ texture(layer_textures[i], uv_y) * weights.y
				+ texture(layer_textures[i], uv_z) * weights.z;
		} else if (layer_mapping[i] == MAPPING_PLANAR) {
			vec2 uv = rect.xy + fract(p.xz) * rect.zw;
			sample_color = texture(layer_textures[i], uv);
		} else if (layer_mapping[i] == MAPPING_BOX) {
			vec3 an = abs(object_normal);
			vec2 uv;
			if (an.x >= an.y && an.x >= an.z) {
				uv = p.yz;
			} else if (an.y >= an.x && an.y >= an.z) {
				uv = p.xz;
			} else {
				uv = p.xy;
			}
			uv = rect.xy + fract(uv) * rect.zw;
			sample_color = texture(layer_textures[i], uv);
		} else { // MAPPING_DECAL: a real projector-box test, not an infinite projection --
			// fragments outside the layer's own unit box (defined by its Scale) contribute nothing,
			// so lower layers show through untouched.
			if (any(greaterThan(abs(p), vec3(0.5)))) {
				coverage = 0.0;
			}
			vec2 uv = rect.xy + (p.xy + vec2(0.5)) * rect.zw;
			sample_color = texture(layer_textures[i], uv);
		}

		float amount = sample_color.a * layer_opacity[i] * coverage;
		result = apply_blend(layer_blend[i], result, sample_color.rgb, amount);
	}

	if (selection_highlight) {
		result = mix(result, selection_highlight_color.rgb, 0.5);
	}

	ALBEDO = result;
}
