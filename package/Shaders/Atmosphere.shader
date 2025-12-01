Shader "Hidden/Atmosphere" {
    
    Properties {
        _MainTex ("Texture", 2D) = "white" {}
    }
    
    SubShader {
        CGINCLUDE
        #include "UnityCG.cginc"
        #include "UnityStandardBRDF.cginc"

        sampler2D _MainTex, _DepthTexture;
        float4 _MainTex_TexelSize;

        struct VertexData {
            float4 vertex : POSITION;
            float2 uv : TEXCOORD0;
        };

        struct v2f {
            float2 uv : TEXCOORD0;
            float4 vertex : SV_POSITION;
        };

        v2f vp(VertexData v) {
            v2f o;
            o.vertex = UnityObjectToClipPos(v.vertex);
            o.uv = v.uv;
            return o;
        }
        ENDCG

        Pass {
            CGPROGRAM
            #pragma vertex vp
            #pragma fragment fp

            float _FogDensity, _FogOffset;
            float3 _FogColor, _SunColor;

            float4x4 _CameraInvViewProjection;

            float3 ComputeWorldSpacePosition(float2 positionNDC, float deviceDepth) {
				float4 positionCS = float4(positionNDC * 2.0 - 1.0, deviceDepth, 1.0);
				float4 hpositionWS = mul(_CameraInvViewProjection, positionCS);
				return hpositionWS.xyz / hpositionWS.w;
			}

            float _FogHeight, _FogAttenuation, _SkyboxSpeed;
            float3 _SunDirection, _SkyboxDirection;

            SamplerState linear_repeat_sampler;
			samplerCUBE _SkyboxTex;

            float4 flowUVW(float3 dir, float3 curl, float t, bool flowB) {
                float phaseOffset = flowB ? 0.5f : 0.0f;
                float progress = t + phaseOffset - floor(t + phaseOffset);
                float3 offset = curl * progress;

                float4 uvw = float4(dir, 0.0f);
                uvw.xz -= offset.xy;
                uvw.w = 1 - abs(1.0f - 2.0f * progress);

                return uvw;
            }


            float4 fp(v2f i) : SV_Target {
                float4 col = tex2D(_MainTex, i.uv);
                float depth = SAMPLE_DEPTH_TEXTURE(_DepthTexture, i.uv);
				float3 worldPos = ComputeWorldSpacePosition(i.uv, depth);
                float3 viewDir = normalize(_WorldSpaceCameraPos - worldPos);

                float3 curl = normalize(_SkyboxDirection);

                float t = _Time.y * _SkyboxSpeed;

                float4 uvw1 = flowUVW(-viewDir, curl, t, false);
                float4 uvw2 = flowUVW(-viewDir, curl, t, true);
                

                float3 sky = texCUBE(_SkyboxTex, uvw1.xyz).rgb * uvw1.w;
                float3 sky2 = texCUBE(_SkyboxTex, uvw2.xyz).rgb * uvw2.w;

                sky = (sky + sky2);

                if (depth == 0) col.rgb = sky;

                float height = min(_FogHeight, worldPos.y) / _FogHeight;
                height = pow(saturate(height), 1.0f / _FogAttenuation);

                depth = Linear01Depth(depth);
                float viewDistance = depth * _ProjectionParams.z;
                
                float fogFactor = (_FogDensity / sqrt(log(2))) * max(0.0f, viewDistance - _FogOffset);
                fogFactor = exp2(-fogFactor * fogFactor);

                float3 sunDir = normalize(_SunDirection);
                float3 sun = _SunColor * pow(DotClamped(viewDir, sunDir), 3500.0f);

                float3 output = lerp(_FogColor, col.rgb, saturate(height + fogFactor));
                
                return float4(output + sun, 1.0f);
            }

            ENDCG
        }
    }
}