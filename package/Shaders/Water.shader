Shader "Custom/Water" {
		
		Properties {
			[Enum(Off, 0, On, 1)] _ZWrite ("Z Write", Float) = 1
		}

	SubShader {
		Tags {
			"LightMode" = "ForwardBase"
		}

		Pass {

			ZWrite On

			CGPROGRAM

			#pragma vertex vp
			#pragma fragment fp

			#pragma shader_feature USE_VERTEX_DISPLACEMENT
			#pragma shader_feature SINE_WAVE
			#pragma shader_feature STEEP_SINE_WAVE
			#pragma shader_feature GERSTNER_WAVE
			#pragma shader_feature NORMALS_IN_PIXEL_SHADER
			#pragma shader_feature CIRCULAR_WAVES
			#pragma shader_feature USE_FBM

			#include "UnityPBSLighting.cginc"
            #include "AutoLight.cginc"

			struct VertexData {
				float4 vertex : POSITION;
				float3 normal : NORMAL;
			};

			struct v2f {
				float4 pos : SV_POSITION;
				float3 normal : TEXCOORD1;
				float3 worldPos : TEXCOORD2;
			};

			struct Wave {
				float2 direction;
				float2 origin;
				float frequency;
				float amplitude;
				float phase;
				float steepness;
				int waveType;
			};
			
			StructuredBuffer<Wave> _Waves;

			//#define sin fastSine

			#define PI 3.14159265358979323846

			float fastSine(float x) {
				return 1.0f;
			}

			float2 GetDirection(float3 v, Wave w) {
				#ifdef CIRCULAR_WAVES
                float2 p = float2(v.x, v.z);

                return normalize(p - w.origin);
				#else
				return w.direction;
				#endif
			}

			float GetWaveCoord(float3 v, float2 d, Wave w) {
				#ifdef CIRCULAR_WAVES
					float2 p = float2(v.x, v.z);
					return length(p - w.origin);
				#endif

				return v.x * d.x + v.z * d.y;
			}

			float GetTime(Wave w) {
				#ifdef CIRCULAR_WAVES
					return -_Time.y * w.phase;
				#else
				return _Time.y * w.phase;
				#endif
			}

			float Sine(float3 v, Wave w) {
				float2 d = GetDirection(v, w);
				float xz = GetWaveCoord(v, d, w);
				float t = GetTime(w);

				return w.amplitude * sin(xz * w.frequency + t);
			}

			float3 SineNormal(float3 v, Wave w) {
				float2 d = GetDirection(v, w);
				float xz = GetWaveCoord(v, d, w);
				float t = GetTime(w);

				float2 n = w.frequency * w.amplitude * d * cos(xz * w.frequency + t);

				return float3(n.x, n.y, 0.0f);
			}

			float SteepSine(float3 v, Wave w) {
				float2 d = GetDirection(v, w);
				float xz = GetWaveCoord(v, d, w);
				float t = GetTime(w);

				return 2.0f * w.amplitude * pow((sin(xz * w.frequency + t) + 1.0f) / 2.0f, w.steepness);
			}

			float3 SteepSineNormal(float3 v, Wave w) {
				float2 d = GetDirection(v, w);
				float xz = GetWaveCoord(v, d, w);
				float t = GetTime(w);

				float h = pow((sin(xz * w.frequency + t) + 1) / 2.0f, max(1.0f, w.steepness - 1));
				float2 n = d * w.steepness * w.frequency * w.amplitude * h * cos(xz * w.frequency + t);

				return float3(n.x, n.y, 0.0f);
			}

			float3 Gerstner(float3 v, Wave w) {
				float2 d = GetDirection(v, w);
				float xz = GetWaveCoord(v, d, w);
				float t = GetTime(w);

				float3 g = float3(0.0f, 0.0f, 0.0f);
				g.x = w.steepness * w.amplitude * d.x * cos(w.frequency * xz + t);
				g.z = w.steepness * w.amplitude * d.y * cos(w.frequency * xz + t);
				g.y = w.amplitude * sin(w.frequency * xz + t);
				
				return g;
			}

			float3 GerstnerNormal(float3 v, Wave w) {
				float2 d = GetDirection(v, w);
				float xz = GetWaveCoord(v, d, w);
				float t = GetTime(w);

				float3 n = float3(0.0f, 0.0f, 0.0f);
				
				float wa = w.frequency * w.amplitude;
				float s = sin(w.frequency * xz + _Time.y * w.phase);
				float c = cos(w.frequency * xz + _Time.y * w.phase);

				n.x = d.x * wa * c;
				n.z = d.y * wa * c;
				n.y = w.steepness * wa * s;

				return n;
			}

			float3 CalculateOffset(float3 v, Wave w) {
				#ifdef SINE_WAVE
					return float3(0.0f, Sine(v, w), 0.0f);
				#endif

				#ifdef STEEP_SINE_WAVE
					return float3(0.0f, SteepSine(v, w), 0.0f);
				#endif

				#ifdef GERSTNER_WAVE
					return Gerstner(v, w);
				#endif

				return 0.0f;
			}

			float3 CalculateNormal(float3 v, Wave w) {
				#ifdef SINE_WAVE
					return SineNormal(v, w);
				#endif

				#ifdef STEEP_SINE_WAVE
					return SteepSineNormal(v, w);
				#endif

				#ifdef GERSTNER_WAVE
					return GerstnerNormal(v, w);
				#endif

				return 0.0f;
			}

			float hash(uint n) {
				// integer hash copied from Hugo Elias
				n = (n << 13U) ^ n;
				n = n * (n * n * 15731U + 0x789221U) + 0x1376312589U;
				return float(n & uint(0x7fffffffU)) / float(0x7fffffff);
			}

			float3 _SunDirection, _SunColor;

			float _VertexSeed, _VertexSeedIter, _VertexFrequency, _VertexFrequencyMult, _VertexAmplitude, _VertexAmplitudeMult, _VertexInitialSpeed, _VertexSpeedRamp, _VertexDrag, _VertexHeight, _VertexMaxPeak, _VertexPeakOffset;
			float _FragmentSeed, _FragmentSeedIter, _FragmentFrequency, _FragmentFrequencyMult, _FragmentAmplitude, _FragmentAmplitudeMult, _FragmentInitialSpeed, _FragmentSpeedRamp, _FragmentDrag, _FragmentHeight, _FragmentMaxPeak, _FragmentPeakOffset;
			float _NormalStrength, _FresnelNormalStrength, _SpecularNormalStrength;
			
			int _WaveCount;
			int _VertexWaveCount;
			int _FragmentWaveCount;

			samplerCUBE _EnvironmentMap;
			int _UseEnvironmentMap;

			float3 vertexFBM(float3 v) {
				float f = _VertexFrequency;
				float a = _VertexAmplitude;
				float speed = _VertexInitialSpeed;
				float seed = _VertexSeed;
				float3 p = v;
				float amplitudeSum = 0.0f;

				float h = 0.0f;
				float2 n = 0.0f;
				for (int wi = 0; wi < _VertexWaveCount; ++wi) {
					float2 d = normalize(float2(cos(seed), sin(seed)));

					float x = dot(d, p.xz) * f + _Time.y * speed;
					float wave = a * exp(_VertexMaxPeak * sin(x) - _VertexPeakOffset);
					float dx = _VertexMaxPeak * wave * cos(x);
					
					h += wave;
					
					p.xz += d * -dx * a * _VertexDrag;

					amplitudeSum += a;
					f *= _VertexFrequencyMult;
					a *= _VertexAmplitudeMult;
					speed *= _VertexSpeedRamp;
					seed += _VertexSeedIter;
				}

				float3 output = float3(h, n.x, n.y) / amplitudeSum;
				output.x *= _VertexHeight;

				return output;
			}

			float3 fragmentFBM(float3 v) {
				float f = _FragmentFrequency;
				float a = _FragmentAmplitude;
				float speed = _FragmentInitialSpeed;
				float seed = _FragmentSeed;
				float3 p = v;

				float h = 0.0f;
				float2 n = 0.0f;
				
				float amplitudeSum = 0.0f;

				for (int wi = 0; wi < _FragmentWaveCount; ++wi) {
					float2 d = normalize(float2(cos(seed), sin(seed)));

					float x = dot(d, p.xz) * f + _Time.y * speed;
					float wave = a * exp(_FragmentMaxPeak * sin(x) - _FragmentPeakOffset);
					float2 dw = f * d * (_FragmentMaxPeak * wave * cos(x));
					
					h += wave;
					p.xz += -dw * a * _FragmentDrag;
					
					n += dw;
					
					amplitudeSum += a;
					f *= _FragmentFrequencyMult;
					a *= _FragmentAmplitudeMult;
					speed *= _FragmentSpeedRamp;
					seed += _FragmentSeedIter;
				}
				
				float3 output = float3(h, n.x, n.y) / amplitudeSum;
				output.x *= _FragmentHeight;

				return output;
			}

			float3 centralDifferenceNormal(float3 v, float epsilon) {
				float2 ex = float2(epsilon, 0);
				float h = fragmentFBM(v).x;
				float3 a = float3(v.x, h, v.z);

				float3 b = a - float3(v.x - epsilon, fragmentFBM(v - ex.xyy).x, v.z);
				float3 c = a - float3(v.x, fragmentFBM(v + ex.yyx).x, v.z + epsilon);

				return normalize(cross(b, c));
			}

			float3 _Ambient, _DiffuseReflectance, _SpecularReflectance, _FresnelColor, _TipColor;
			float _Shininess, _FresnelBias, _FresnelStrength, _FresnelShininess, _TipAttenuation;

			float4x4 _CameraInvViewProjection;
			sampler2D _CameraDepthTexture;

			v2f vp(VertexData v) {
				v2f i;

				#ifdef USE_VERTEX_DISPLACEMENT
					i.worldPos = mul(unity_ObjectToWorld, v.vertex);

					float3 h = 0.0f;
					float3 n = 0.0f;

					#ifdef USE_FBM
					float3 fbm = vertexFBM(i.worldPos);

					h.y = fbm.x;
					n.xy = fbm.yz;
					#else
					for (int wi = 0; wi < _WaveCount; ++wi) {
						h += CalculateOffset(i.worldPos, _Waves[wi]);

						#ifndef GERSTNER_WAVE
							#ifndef NORMALS_IN_PIXEL_SHADER
								n += CalculateNormal(i.worldPos, _Waves[wi]);
							#endif
						#endif
					}
					#endif

					float4 newPos = v.vertex + float4(h, 0.0f);
					i.worldPos = mul(unity_ObjectToWorld, newPos);
					i.pos = UnityObjectToClipPos(newPos);
					
					#ifndef NORMALS_IN_PIXEL_SHADER
					#ifdef GERSTNER_WAVE
						for (int wi = 0; wi < _WaveCount; ++wi) {
							n += CalculateNormal(i.worldPos, _Waves[wi]);
						}

                        i.normal = normalize(UnityObjectToWorldNormal(normalize(float3(-n.x, 1.0f - n.y, -n.z))));
					#else
						i.normal = normalize(UnityObjectToWorldNormal(normalize(float3(-n.x, 1.0f, -n.y))));
					#endif
					#else
						i.normal = 0.0;
					#endif
				#else
					i.worldPos = mul(unity_ObjectToWorld, v.vertex);
					i.normal = normalize(UnityObjectToWorldNormal(v.normal));
					i.pos = UnityObjectToClipPos(v.vertex);
				#endif

				return i;
			}

			float3 ComputeWorldSpacePosition(float2 positionNDC, float deviceDepth) {
				float4 positionCS = float4(positionNDC * 2.0 - 1.0, deviceDepth, 1.0);
				float4 hpositionWS = mul(_CameraInvViewProjection, positionCS);
				return hpositionWS.xyz / hpositionWS.w;
			}

			float4 fp(v2f i) : SV_TARGET {
                float3 lightDir = -normalize(_SunDirection);
                float3 viewDir = normalize(_WorldSpaceCameraPos - i.worldPos);
                float3 halfwayDir = normalize(lightDir + viewDir);

				float3 normal = 0.0f;
				float height = 0.0f;

				#ifdef NORMALS_IN_PIXEL_SHADER

				#ifdef USE_FBM
					float3 fbm = fragmentFBM(i.worldPos);
					height = fbm.x;
					normal.xy = fbm.yz;
				#else
				for (int wi = 0; wi < _WaveCount; ++wi) {
					normal += CalculateNormal(i.worldPos, _Waves[wi]);
				}
				#endif
				#ifdef GERSTNER_WAVE
					normal = normalize(UnityObjectToWorldNormal(normalize(float3(-normal.x, 1.0f - normal.y, -normal.z))));
				#else
					normal = normalize(UnityObjectToWorldNormal(normalize(float3(-normal.x, 1.0f, -normal.y))));
				#endif

				#else
					normal = normalize(i.normal);
				#endif

				// normal = centralDifferenceNormal(i.worldPos, 0.01f);
				normal.xz *= _NormalStrength;
				normal = normalize(normal);

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
				


				float3 tipColor = _TipColor * pow(height, _TipAttenuation);

				float3 output = _Ambient + diffuse + specular + fresnel + tipColor;


				return float4(output, 1.0f);
			}

			ENDCG
		}
	}
}