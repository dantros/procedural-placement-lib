in vec3 f_normal;
in vec3 f_position;
in vec3 f_base_color;

uniform vec3 u_view_position;
uniform vec3 u_light_color      = { 1., 1., 1. };
uniform vec3 u_light_position   = { 0., 0., 1000. };
uniform float u_ambient_light_intensity     = 0.1f;
uniform float u_specular_light_intensity    = 0.5f;

void main()
{
    const vec3 normal = normalize(f_normal);
    const vec3 light_direction = normalize(u_light_position - f_position);

    const float diffuse_light_intensity = max(dot(normal, light_direction), 0.f);

    const vec3 view_direction = normalize(u_view_position - f_position);
    const vec3 reflect_direction = reflect(-light_direction, normal);
    const float spec = pow(max(dot(view_direction, reflect_direction), 0.f), 32);

    const vec3 color_sum = (u_ambient_light_intensity + diffuse_light_intensity + u_specular_light_intensity * spec)
    * u_light_color * f_base_color;

    frag_color = vec4(color_sum, 1.0f);
}