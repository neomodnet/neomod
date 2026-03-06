Texture2D<float4> tex0 : register(t0, space2);
SamplerState samp0 : register(s0, space2);

cbuffer FragUniforms : register(b0, space3) {
    float4 misc; // x = texturing enabled, y = color inversion, z/w = unused
    float4 col;  // global color (m_color)
};

struct PSInput {
    float4 position : SV_Position;
    float4 fragColor : TEXCOORD0;
    float2 fragTexcoord : TEXCOORD1;
};

float4 main(PSInput input) : SV_Target0 {
    float4 result;
    if (misc.x > 0.5) {
        result = tex0.Sample(samp0, input.fragTexcoord) * col * input.fragColor;
    } else {
        result = input.fragColor;
    }

    if (misc.y > 0.5) {
        result.rgb = float3(1.0, 1.0, 1.0) - result.rgb;
    }

    return result;
}
