Shader "Custom/FFTWater" {
		
		Properties {
			[Enum(Off, 0, On, 1)] _ZWrite ("Z Write", Float) = 1
		}

	CGINCLUDE
        #define _TessellationEdgeLength 10
		#define NEW_LIGHTING

        struct TessellationFactors {
            float edge[3] : SV_TESSFACTOR;
            float inside : SV_INSIDETESSFACTOR;
        };

        float TessellationHeuristic(float3 cp0, float3 cp1) {
            float edgeLength = distance(cp0, cp1);
            float3 edgeCenter = (cp0 + cp1) * 0.5;
            float viewDistance = distance(edgeCenter, _WorldSpaceCameraPos);

            return edgeLength * _ScreenParams.y / (_TessellationEdgeLength * (pow(viewDistance * 0.5f, 1.2f)));
        }

        bool TriangleIsBelowClipPlane(float3 p0, float3 p1, float3 p2, int planeIndex, float bias) {
            float4 plane = unity_CameraWorldClipPlanes[planeIndex];

            return dot(float4(p0, 1), plane) < bias && dot(float4(p1, 1), plane) < bias && dot(float4(p2, 1), plane) < bias;
        }

        bool cullTriangle(float3 p0, float3 p1, float3 p2, float bias) {
            return TriangleIsBelowClipPlane(p0, p1, p2, 0, bias) ||
                   TriangleIsBelowClipPlane(p0, p1, p2, 1, bias) ||
                   TriangleIsBelowClipPlane(p0, p1, p2, 2, bias) ||
                   TriangleIsBelowClipPlane(p0, p1, p2, 3, bias);
        }
    ENDCG

	SubShader {
		Tags {
			"LightMode" = "ForwardBase"
		}

		Pass {

			CGPROGRAM



			#pragma vertex dummyvp
			#pragma hull hp
			#pragma domain dp 
			#pragma geometry gp
			#pragma fragment fp


			#include "UnityPBSLighting.cginc"
            #include "AutoLight.cginc"

			struct TessellationControlPoint {
                float4 vertex : INTERNALTESSPOS;
                float2 uv : TEXCOORD0;
            };

			struct VertexData {
				float4 vertex : POSITION;
                float2 uv : TEXCOORD0;
			};

			struct v2g {
				float4 pos : SV_POSITION;
                float2 uv : TEXCOORD0;
				float3 worldPos : TEXCOORD1;
				float depth : TEXCOORD2;
			};


			#define PI 3.14159265358979323846

			float hash(uint n) {
				// integer hash copied from Hugo Elias
				n = (n << 13U) ^ n;
				n = n * (n * n * 15731U + 0x789221U) + 0x1376312589U;
				return float(n & uint(0x7fffffffU)) / float(0x7fffffff);
			}

			float3 _SunDirection, _SunColor;

			float _NormalStrength, _FresnelNormalStrength, _SpecularNormalStrength;

			samplerCUBE _EnvironmentMap;
			int _UseEnvironmentMap;

			float3 _Ambient, _DiffuseReflectance, _SpecularReflectance, _FresnelColor, _TipColor;
			float _Shininess, _FresnelBias, _FresnelStrength, _FresnelShininess, _TipAttenuation;
			float _Roughness, _FoamRoughnessModifier;
			float _Tile0, _Tile1, _Tile2, _Tile3;
			float3 _SunIrradiance, _ScatterColor, _BubbleColor, _FoamColor;
			float _HeightModifier, _BubbleDensity;
			float _DisplacementDepthAttenuation, _FoamDepthAttenuation, _NormalDepthAttenuation;
			float _WavePeakScatterStrength, _ScatterStrength, _ScatterShadowStrength, _EnvironmentLightStrength;

			int _DebugTile0, _DebugTile1, _DebugTile2, _DebugTile3;
			int _ContributeDisplacement0, _ContributeDisplacement1, _ContributeDisplacement2, _ContributeDisplacement3;
			int _DebugLayer0, _DebugLayer1, _DebugLayer2, _DebugLayer3;
			float _FoamSubtract0, _FoamSubtract1, _FoamSubtract2, _FoamSubtract3;

			float4x4 _CameraInvViewProjection;
			sampler2D _CameraDepthTexture;
            UNITY_DECLARE_TEX2DARRAY(_DisplacementTextures);
            UNITY_DECLARE_TEX2DARRAY(_SlopeTextures);
            SamplerState point_repeat_sampler, linear_repeat_sampler, trilinear_repeat_sampler;

            float _Tile;

			TessellationControlPoint dummyvp(VertexData v) {
				TessellationControlPoint p;
				p.vertex = v.vertex;
				p.uv = v.uv;

				return p;
			}

			v2g vp(VertexData v) {
				v2g g;
				v.uv = 0;
                g.worldPos = mul(unity_ObjectToWorld, v.vertex);

                float3 displacement1 = UNITY_SAMPLE_TEX2DARRAY_LOD(_DisplacementTextures, float3(g.worldPos.xz * _Tile0, 0), 0) * _DebugLayer0 * _ContributeDisplacement0;
                float3 displacement2 = UNITY_SAMPLE_TEX2DARRAY_LOD(_DisplacementTextures, float3(g.worldPos.xz * _Tile1, 1), 0) * _DebugLayer1 * _ContributeDisplacement1;
                float3 displacement3 = UNITY_SAMPLE_TEX2DARRAY_LOD(_DisplacementTextures, float3(g.worldPos.xz * _Tile2, 2), 0) * _DebugLayer2 * _ContributeDisplacement2;
                float3 displacement4 = UNITY_SAMPLE_TEX2DARRAY_LOD(_DisplacementTextures, float3(g.worldPos.xz * _Tile3, 3), 0) * _DebugLayer3 * _ContributeDisplacement3;
				float3 displacement = displacement1 + displacement2 + displacement3 + displacement4;

				float4 clipPos = UnityObjectToClipPos(v.vertex);
				float depth = 1 - Linear01Depth(clipPos.z / clipPos.w);

				displacement = lerp(0.0f, displacement, pow(saturate(depth), _DisplacementDepthAttenuation));

				v.vertex.xyz += mul(unity_WorldToObject, displacement.xyz);
				
                g.pos = UnityObjectToClipPos(v.vertex);
                g.uv = g.worldPos.xz;
                g.worldPos = mul(unity_ObjectToWorld, v.vertex);
				g.depth = depth;
				return g;
			}

			struct g2f {
				v2g data;
				float2 barycentricCoordinates : TEXCOORD9;
			};

			TessellationFactors PatchFunction(InputPatch<TessellationControlPoint, 3> patch) {
                float3 p0 = mul(unity_ObjectToWorld, patch[0].vertex);
                float3 p1 = mul(unity_ObjectToWorld, patch[1].vertex);
                float3 p2 = mul(unity_ObjectToWorld, patch[2].vertex);

                TessellationFactors f;
                float bias = -0.5 * 100;
                if (cullTriangle(p0, p1, p2, bias)) {
                    f.edge[0] = f.edge[1] = f.edge[2] = f.inside = 0;
                } else {
                    f.edge[0] = TessellationHeuristic(p1, p2);
                    f.edge[1] = TessellationHeuristic(p2, p0);
                    f.edge[2] = TessellationHeuristic(p0, p1);
                    f.inside = (TessellationHeuristic(p1, p2) +
                                TessellationHeuristic(p2, p0) +
                                TessellationHeuristic(p1, p2)) * (1 / 3.0);
                }
                return f;
            }

            [UNITY_domain("tri")]
            [UNITY_outputcontrolpoints(3)]
            [UNITY_outputtopology("triangle_cw")]
            [UNITY_partitioning("integer")]
            [UNITY_patchconstantfunc("PatchFunction")]
            TessellationControlPoint hp(InputPatch<TessellationControlPoint, 3> patch, uint id : SV_OUTPUTCONTROLPOINTID) {
                return patch[id];
            }

            [maxvertexcount(3)]
            void gp(triangle v2g g[3], inout TriangleStream<g2f> stream) {
                g2f g0, g1, g2;
                g0.data = g[0];
                g1.data = g[1];
                g2.data = g[2];

                g0.barycentricCoordinates = float2(1, 0);
                g1.barycentricCoordinates = float2(0, 1);
                g2.barycentricCoordinates = float2(0, 0);

                stream.Append(g0);
                stream.Append(g1);
                stream.Append(g2);
            }

            #define DP_INTERPOLATE(fieldName) data.fieldName = \
                data.fieldName = patch[0].fieldName * barycentricCoordinates.x + \
                                 patch[1].fieldName * barycentricCoordinates.y + \
                                 patch[2].fieldName * barycentricCoordinates.z;               

            [UNITY_domain("tri")]
            v2g dp(TessellationFactors factors, OutputPatch<TessellationControlPoint, 3> patch, float3 barycentricCoordinates : SV_DOMAINLOCATION) {
                VertexData data;
                DP_INTERPOLATE(vertex)
                DP_INTERPOLATE(uv)

                return vp(data);
            }

			float SchlickFresnel(float3 normal, float3 viewDir) {
				// 0.02f comes from the reflectivity bias of water kinda idk it's from a paper somewhere i'm not gonna link it tho lmaooo
				return 0.02f + (1 - 0.02f) * (pow(1 - DotClamped(normal, viewDir), 5.0f));
			}

			float SmithMaskingBeckmann(float3 H, float3 S, float roughness) {
				float hdots = max(0.001f, DotClamped(H, S));
				float a = hdots / (roughness * sqrt(1 - hdots * hdots));
				float a2 = a * a;

				return a < 1.6f ? (1.0f - 1.259f * a + 0.396f * a2) / (3.535f * a + 2.181 * a2) : 0.0f;
			}

			float Beckmann(float ndoth, float roughness) {
				float exp_arg = (ndoth * ndoth - 1) / (roughness * roughness * ndoth * ndoth);

				return exp(exp_arg) / (PI * roughness * roughness * ndoth * ndoth * ndoth * ndoth);
			}

			float4 fp(g2f f) : SV_TARGET {
                float3 lightDir = -normalize(_SunDirection);
                float3 viewDir = normalize(_WorldSpaceCameraPos - f.data.worldPos);
                float3 halfwayDir = normalize(lightDir + viewDir);
				float depth = f.data.depth;
				float LdotH = DotClamped(lightDir, halfwayDir);
				float VdotH = DotClamped(viewDir, halfwayDir);
				
				
                float4 displacementFoam1 = UNITY_SAMPLE_TEX2DARRAY(_DisplacementTextures, float3(f.data.uv * _Tile0, 0)) * _DebugLayer0;
				displacementFoam1.a += _FoamSubtract0;
                float4 displacementFoam2 = UNITY_SAMPLE_TEX2DARRAY(_DisplacementTextures, float3(f.data.uv * _Tile1, 1)) * _DebugLayer1;
				displacementFoam2.a += _FoamSubtract1;
                float4 displacementFoam3 = UNITY_SAMPLE_TEX2DARRAY(_DisplacementTextures, float3(f.data.uv * _Tile2, 2)) * _DebugLayer2;
				displacementFoam3.a += _FoamSubtract2;
                float4 displacementFoam4 = UNITY_SAMPLE_TEX2DARRAY(_DisplacementTextures, float3(f.data.uv * _Tile3, 3)) * _DebugLayer3;
				displacementFoam4.a += _FoamSubtract3;
                float4 displacementFoam = displacementFoam1 + displacementFoam2 + displacementFoam3 + displacementFoam4;

				
				float2 slopes1 = UNITY_SAMPLE_TEX2DARRAY(_SlopeTextures, float3(f.data.uv * _Tile0, 0)) * _DebugLayer0;
				float2 slopes2 = UNITY_SAMPLE_TEX2DARRAY(_SlopeTextures, float3(f.data.uv * _Tile1, 1)) * _DebugLayer1;
				float2 slopes3 = UNITY_SAMPLE_TEX2DARRAY(_SlopeTextures, float3(f.data.uv * _Tile2, 2)) * _DebugLayer2;
				float2 slopes4 = UNITY_SAMPLE_TEX2DARRAY(_SlopeTextures, float3(f.data.uv * _Tile3, 3)) * _DebugLayer3;
				float2 slopes = slopes1 + slopes2 + slopes3 + slopes4;

				
				slopes *= _NormalStrength;
				float foam = lerp(0.0f, saturate(displacementFoam.a), pow(depth, _FoamDepthAttenuation));

				#ifdef NEW_LIGHTING
				float3 macroNormal = float3(0, 1, 0);
				float3 mesoNormal = normalize(float3(-slopes.x, 1.0f, -slopes.y));
				mesoNormal = normalize(lerp(float3(0, 1, 0), mesoNormal, pow(saturate(depth), _NormalDepthAttenuation)));
				mesoNormal = normalize(UnityObjectToWorldNormal(normalize(mesoNormal)));

				float NdotL = DotClamped(mesoNormal, lightDir);

				
				float a = _Roughness + foam * _FoamRoughnessModifier;
				float ndoth = max(0.0001f, dot(mesoNormal, halfwayDir));

				float viewMask = SmithMaskingBeckmann(halfwayDir, viewDir, a);
				float lightMask = SmithMaskingBeckmann(halfwayDir, lightDir, a);
				
				float G = rcp(1 + viewMask + lightMask);

				float eta = 1.33f;
				float R = ((eta - 1) * (eta - 1)) / ((eta + 1) * (eta + 1));
				float thetaV = acos(viewDir.y);

				float numerator = pow(1 - dot(mesoNormal, viewDir), 5 * exp(-2.69 * a));
				float F = R + (1 - R) * numerator / (1.0f + 22.7f * pow(a, 1.5f));
				F = saturate(F);
				
				float3 specular = _SunIrradiance * F * G * Beckmann(ndoth, a);
				specular /= 4.0f * max(0.001f, DotClamped(macroNormal, lightDir));
				specular *= DotClamped(mesoNormal, lightDir);

				float3 envReflection = texCUBE(_EnvironmentMap, reflect(-viewDir, mesoNormal)).rgb;
				envReflection *= _EnvironmentLightStrength;

				float H = max(0.0f, displacementFoam.y) * _HeightModifier;
				float3 scatterColor = _ScatterColor;
				float3 bubbleColor = _BubbleColor;
				float bubbleDensity = _BubbleDensity;

				
				float k1 = _WavePeakScatterStrength * H * pow(DotClamped(lightDir, -viewDir), 4.0f) * pow(0.5f - 0.5f * dot(lightDir, mesoNormal), 3.0f);
				float k2 = _ScatterStrength * pow(DotClamped(viewDir, mesoNormal), 2.0f);
				float k3 = _ScatterShadowStrength * NdotL;
				float k4 = bubbleDensity;

				float3 scatter = (k1 + k2) * scatterColor * _SunIrradiance * rcp(1 + lightMask);
				scatter += k3 * scatterColor * _SunIrradiance + k4 * bubbleColor * _SunIrradiance;

				
				float3 output = (1 - F) * scatter + specular + F * envReflection;
				output = max(0.0f, output);
				output = lerp(output, _FoamColor, saturate(foam));

				#else
				slopes *= _NormalStrength;
				float3 normal = normalize(float3(-slopes.x, 1.0f, -slopes.y));
                normal = normalize(UnityObjectToWorldNormal(normalize(normal)));

				float ndotl = DotClamped(lightDir, normal);

				float3 diffuseReflectance = _DiffuseReflectance / PI;
                float3 diffuse = _LightColor0.rgb * ndotl * diffuseReflectance;

				// Schlick Fresnel
				float3 fresnelNormal = normal;
				fresnelNormal.xz *= _FresnelNormalStrength;
				fresnelNormal = normalize(fresnelNormal);
				float base = 1 - dot(viewDir, fresnelNormal);
				float exponential = pow(base, _FresnelShininess);
				float R = exponential + _FresnelBias * (1.0f - exponential);
				R *= _FresnelStrength;
				
				float3 fresnel = _FresnelColor * R;
                
				if (_UseEnvironmentMap) {
					float3 reflectedDir = reflect(-viewDir, normal);
					float3 skyCol = texCUBE(_EnvironmentMap, reflectedDir).rgb;
					float3 sun = _SunColor * pow(max(0.0f, DotClamped(reflectedDir, lightDir)), 500.0f);

					fresnel = skyCol.rgb * R;
					fresnel += sun * R;
				}


				float3 specularReflectance = _SpecularReflectance;
				float3 specNormal = normal;
				specNormal.xz *= _SpecularNormalStrength;
				specNormal = normalize(specNormal);
				float spec = pow(DotClamped(specNormal, halfwayDir), _Shininess) * ndotl;
                float3 specular = _LightColor0.rgb * specularReflectance * spec;

				// Schlick Fresnel but again for specular
				base = 1 - DotClamped(viewDir, halfwayDir);
				exponential = pow(base, 5.0f);
				R = exponential + _FresnelBias * (1.0f - exponential);

				specular *= R;
				

				float3 output = _Ambient + diffuse + specular + fresnel;
				output = lerp(output, _TipColor, saturate(foam));
				#endif


				if (_DebugTile0) {
					output = cos(f.data.uv.x * _Tile0 * PI) * cos(f.data.uv.y * _Tile0 * PI);
				}

				if (_DebugTile1) {
					output = cos(f.data.uv.x * _Tile1) * 1024 * cos(f.data.uv.y * _Tile1) * 1024;
				}

				if (_DebugTile2) {
					output = cos(f.data.uv.x * _Tile2) * 1024 * cos(f.data.uv.y * _Tile2) * 1024;
				}

				if (_DebugTile3) {
					output = cos(f.data.uv.x * _Tile3) * 1024 * cos(f.data.uv.y * _Tile3) * 1024;
				}
				
				return float4(output, 1.0f);
			}

			ENDCG
		}
	}
}