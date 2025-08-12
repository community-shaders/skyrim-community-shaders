#include "Common/FrameBuffer.hlsli"
#include "Common/Math.hlsli"
#include "Common/CoordMath.hlsli"
#include "Common/Random.hlsli"
#include "Common/Color.hlsli"

//// Structs ////////////////////////////////////////////////////////////////////////////

struct VertexShaderInput
{
    float4 Position : POSITION;
    float2 TexCoord : TEXCOORD;
};

struct VertexShaderOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

struct MaskVSOutput
{
    float4 Position : SV_POSITION;
};

struct StarburstVSOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    nointerpolation float4 SunInt : DATA1;
    nointerpolation float4 Color : DATA2;
    nointerpolation float  Scale : DATA3;
    nointerpolation float2 ApertureBlades[16] : DATA4;
};

struct GhostVSOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    float4 AtlasCoords : TEXCOORD1;
    nointerpolation float4 SunInt : DATA1;
    nointerpolation float4 Color : DATA2;
    nointerpolation float2 CADispacement : DATA4;
    nointerpolation float2 Vertices[10] : DATA6;
};

struct SunGlareVertexOutput
{
    float4 Position : SV_POSITION;
    float4 TexCoord : TEXCOORD0;
    nointerpolation float4 SunInt : DATA0;
    nointerpolation float3 Color : DATA1;
    nointerpolation float4 SkyColor : DATA2;
    nointerpolation float Scale : DATA3;
};

struct HaloVertexOutput
{
    float4 Position : SV_POSITION;
    float4 TexCoord : TEXCOORD0;
    nointerpolation float3 SunColor : DATA0;
    nointerpolation float4 SunInt : DATA1;
};

struct LensGlareVertexOutput
{
    float4 Position : SV_POSITION;
    float4 TexCoord : TEXCOORD0;
    nointerpolation float3 SunColor : DATA0;
    nointerpolation float4 SunInt : DATA1;
};

struct IceVertexOutput
{
    float4 Position : SV_POSITION;
    float4 TexCoord : TEXCOORD0;
    float3 Color : DATA0;
};
/////////////////////////////////////////////////////////////////////////////////////////



//// Resources //////////////////////////////////////////////////////////////////////////

cbuffer Settings : register(b1){
    float4 ScreenSize;
    uint  Frame;
    float Precip;
    float WeatherBasedFadeout;
    float4 SunParams;
    float4 SunBlendColor;

    float UIBurstScale;
    float UIBurstInt;

    uint  UIEnableBlades;
    float UIBladeInt;
    float UIBladeVerts;
    float UIBladeSplay;
    float UIBladeRotation;
    float UIBladeLength;
    float UIBladeBaseWidth;
    float UIBladeWidth;
    float UIBladeTaper;
    float UIBladeFeather;
    float UIBladeFadePow;
    float UIBladeFadeDist;
    float BladeSplayLen;

    uint  UIEnableRays;
    float UIRaysInt;
    float UIRaysVolume;
    float UIRaysLength;
    float UIRaysWidth;

    float UIGhostScale;
    float UIGhostInt;
    float UIGhostSat;
    uint  UIGhostClampEnable;
    float UIGhostClampOffset;

    float UIGhostSize;
    float UIGhostOffset;
    float UIGhostShape;
    float UIGhostRoundness;
    float UIGhostRotation;
	float UIGhostFeather;
	float UIGhostCAFactor;
	float UIGhostMoveCurve;
    float UIGhostInsideInt;

    float UIGlareScale;
    float UIGlareInt;
    float UIGlareXOffset;
    float UIGlareYOffset;
    float UIGlareMaxRot;
    float UIGlareCutDepth;
    float UIGlareRadius;
    float UIGlareTipFade;

    float UIHaloScale;
    float UIHaloInt;
    uint  UIHaloEnableExp;
    uint  UIHaloFlipExpOffset;
    float UIHaloExpMinSize;
    float UIHaloExpMaxSize;
    float UIHaloRotationSpeed;
    float UIHaloIncr;
    float UIHaloLength;
    float UIHaloWidth;
    float UIHaloTaper;
    float UIHaloCrShift;

    float UISunGlareScale;
    float UISunGlareInt;
    float UISunGlareOuterInt;
    float UISunGlareFade;

    float UICAIntensity;
    float UICAThreshold;
    float UICAMaxOffset;

    float UIFrostInt;
    float SnowPrecipValue;

    float4 UIBurstColor;
    float4 UISunGlareColor;
    float4 UIHaloColor;
    float4 UIColor;
    float4 UIGhostAtlas;
    float4 UIFrostColor;
};

SamplerState Linear_Sampler : register(s10);
SamplerState Point_Sampler : register(s11);
SamplerState PointMirror_Sampler : register(s12);
SamplerComparisonState Depth_Sampler : register(s13);

/////////////////////////////////////////////////////////////////////////////////////////



//// Safe Macros ////////////////////////////////////////////////////////////////////////

static const float SUNCLIP = 0.05;
static const int OCCLSAMPLES = 20;

inline float  inv(float x) {return 1.0 - x;}
inline float2 inv(float2 x) {return 1.0 - x;}
inline float3 inv(float3 x) {return 1.0 - x;}
inline float  delta(float x) {return max(x, EPSILON_DIVISION);}
inline float2 delta(float2 x) {return max(x, EPSILON_DIVISION);}
inline float  LinearStep(float x, float y, float z) {return Math::LinearStep(x, y, z);}
inline float2 LinearStep(float2 x, float2 y, float2 z) {return Math::LinearStep(x, y, z);}
inline float3 LinearStep(float3 x, float3 y, float3 z) {return Math::LinearStep(x, y, z);}
inline float2 DegreesToVector(float x) {return CoordMath::DegreesToVector(x);}
inline float  Luma(float3 x) {return Color::RGBToLuminance(x);}
inline float  Chroma(float3 x) {return Color::RGBToChrominance(x);}

/////////////////////////////////////////////////////////////////////////////////////////



///// Occlusion Shader //////////////////////////////////////////////////////////////////

#ifdef OCCLUSION_PIXEL_SHADER

cbuffer CB2 : register(b2){
    float4 buffer[16];};

Texture2D DepthTexture : register(t0);
Texture2D ColorTexture : register(t1);
RWTexture2D<float4> SunLUT : register(u0);
RWTexture2D<uint> SunLUT_AT : register(u1);
Texture2D MotionVector : register(t2);


static const int nFrames = 10;

uint GetTemporalAverage(uint Value, uint idx){
    SunLUT_AT[int2(idx-1, 0)] = Value;

    uint Sum = 0.0;
    [unroll] for(int i=0; i < nFrames; ++i)
        Sum += SunLUT_AT.Load(int2(5+i, 0));

    return Sum / nFrames;
}

float GetTemporalAverage(float Value, uint idx){
    SunLUT[int2(idx-1, 0)] = Value.xxxx;

    float Sum = 0.0;
    [unroll] for(int i=0; i < nFrames; ++i)
        Sum += SunLUT.Load(int2(5+i, 0)).x;

    return Sum / nFrames;
}

float UpdateDepthFactor(float2 SunCoords, float SunRadius){
    float DepthFactor = 0.00001;
    [loop] for(int i=0; i<OCCLSAMPLES; ++i){
        float2 Offset = Math::sincos2(Random::RandomSH(i) * Math::TAU) * sqrt(Random::RandomSH(i+1)) * SunRadius;
        float2 Coords = SunCoords + Offset;
        DepthFactor += DepthTexture.SampleCmpLevelZero(Depth_Sampler, Coords, 1).x;
    }
    DepthFactor = LinearStep(0.0, 0.9, DepthFactor / OCCLSAMPLES);

    return GetTemporalAverage(DepthFactor, Frame);
}

float UpdateCloudFactor(float2 SunSSCoords, uint CloudFactor, uint OldCloudFactor){
    bool InsideSSBoundry = CoordMath::InsideRect(SunSSCoords, float2(200,200), ScreenSize.xy-200);
    float Motion = Math::max2(MotionVector.Sample(Point_Sampler, SunSSCoords).xy);

    if(InsideSSBoundry && Motion < 0.04){
        SunLUT_AT[int2(0,0)] = 0;
        SunLUT_AT[int2(1,0)] = CloudFactor;
    } else{
        CloudFactor = OldCloudFactor; }

    return float(GetTemporalAverage(CloudFactor, Frame));
}

float GetWeatherFactor(float4 SunColor, float CloudFactor, float SunRadius){
    float PrecipFactor = 1.0 - LinearStep(0.2, 0.6, Precip);
    float WeatherFade = inv(WeatherBasedFadeout);

    CloudFactor = saturate(inv(CloudFactor / delta((Math::PI * (SunRadius * SunRadius)))));
    CloudFactor = LinearStep(0.0, 0.8, CloudFactor);

    return PrecipFactor * CloudFactor * WeatherFade;
}

float GetSunRadius(float SunDepth, float SunScale, out float SunUVRadius){
    float FocalScale = FrameBuffer::CameraProj[0][1][1];

    float SunSSRadius = SunScale * (FocalScale / SunDepth) * (ScreenSize.y * 0.5) * 0.5;
    SunUVRadius = SunSSRadius * rcp(ScreenSize.x);

    return SunSSRadius;
}


void main(MaskVSOutput input)
{
    float index = (int)input.Position.x;
    float2 SunCoords = buffer[0].xy;
    float2 SunSSCoords = SunCoords * ScreenSize.xy;

    [branch] if(index < 1 && min(SunCoords.x, SunCoords.y) > -0.4)
    {
        uint Clouds = SunLUT_AT.Load(int2(0,0));
        uint OldClouds = SunLUT_AT.Load(int2(1,0));

        float SunUVRadius;
        float SunSSRadius = GetSunRadius(SunParams.z, SunParams.w, SunUVRadius);
        if(SunUVRadius < 0.0 || SunSSRadius < 0.0){
            float4 Sun = SunLUT.Load(int2(3,0));
            SunSSRadius = Sun.z;
            SunUVRadius = SunSSRadius * rcp(ScreenSize.x);
        }

        float CloudFactor = UpdateCloudFactor(SunSSCoords, Clouds, OldClouds);
        float DepthFactor = UpdateDepthFactor(SunCoords, SunUVRadius);

        float WeatherFactor = GetWeatherFactor(SunBlendColor, CloudFactor, SunSSRadius);

        float DistFactor = CoordMath::ChebyDistance(float2(SunCoords.x, 1.0 - SunCoords.y) * 2.0 - 1.0);
              DistFactor = LinearStep(0.0, 0.65, inv(saturate(DistFactor-0.2)));


        SunLUT[int2(0,0)] = float4(DepthFactor, WeatherFactor, DistFactor, DepthFactor * WeatherFactor * DistFactor);
        SunLUT[int2(1,0)] = float4(saturate(SunBlendColor.xyz + 0.3), SunUVRadius * 2);
        SunLUT[int2(3,0)] = float4(SunSSCoords, SunSSRadius, SunSSRadius * 1.2);
    }
}
#endif
/////////////////////////////////////////////////////////////////////////////////////////



//// Starburst Vertex Shader ////////////////////////////////////////////////////////////

#ifdef STARBURST_VERTEX_SHADER

cbuffer SunFlare : register(b2) {
    float4 Unused;
    float4 Unused2;
    float4 SunPositionUV;
};
Texture2D OcclusionLUT : register(t0);


StarburstVSOutput main(VertexShaderInput input)
{
	StarburstVSOutput output;
    output.TexCoord = input.TexCoord.xy * 2.0 - 1.0;
    output.Position = float4(input.Position.xy, 0.0, 1.0);

    float4 SunParams = OcclusionLUT.Load(int3(1,0,0));
    float SunRadius = SunParams.w;

    float BurstScale = Math::MapRange(SunRadius, 0.02, 0.05, UIBurstScale * 0.75, UIBurstScale);
    output.Scale = SunRadius * rcp(BurstScale);

    output.Position.y *= ScreenSize.z;
    output.Position.xy = mad(output.Position.xy, BurstScale, SunPositionUV.xy * 2.0 - 1.0);

    [unroll] for(int i=0; i<16; ++i)
		output.ApertureBlades[i] = DegreesToVector(360.0 / UIBladeVerts * (i + 0.5) + UIBladeRotation);
	output.ApertureBlades[15] = output.ApertureBlades[0];

    output.SunInt = OcclusionLUT.Load(0);
    output.SunInt.y = smoothstep(0.15, 0.35, output.SunInt.z) * LinearStep(0.6, 0.8, output.SunInt.y);

    float4 SkyColor = OcclusionLUT.Load(int3(2,0,0));
    float ColorScale = lerp(0.8, 2.0, LinearStep(0.2, 0.4, Chroma(SkyColor.xyz)));

    output.Color.xyz = UIBurstColor.xyz;
    output.Color.w = Math::MapRange(SunRadius, 0.02, 0.05, 0.5, 1.0) * ColorScale;


    return output;
}
#endif
/////////////////////////////////////////////////////////////////////////////////////////



//// Starburst Pixel Shader /////////////////////////////////////////////////////////////

#ifdef STARBURST_PIXEL_SHADER

Texture2D depth : register(t0);


float4 main(StarburstVSOutput input) : SV_Target
{
    float Dist = length(input.TexCoord.xy);
    float InvDist = 1.0 - Dist;

    clip(min(input.SunInt.w - SUNCLIP, InvDist));

    float CoronaDist = saturate(Dist - input.Scale);
    float SunBorder = smoothstep(0.0, 0.01, CoronaDist);
    float BladeMask = 0.0, RayMask = 0.0;

    float Depth = depth.SampleCmpLevelZero(Depth_Sampler, input.Position.xy / ScreenSize.xy, 1).x;


    [branch] if(UIEnableBlades){
        float2 Normal = normalize(input.TexCoord.xy);
        float InvCoronaDist = 1.0 - CoronaDist;
        float AngularDist = 1.0;

        [loop] for(int i=0; i<UIBladeVerts; ++i)
            AngularDist = min(AngularDist, distance(Normal, input.ApertureBlades[i]));
        AngularDist = 1.0 - AngularDist;

        float BladeWidth = pow(CoronaDist, inv(UIBladeBaseWidth)) * UIBladeWidth;

        float BladeLength = pow(abs(Dist * rcp(UIBladeLength)), UIBladeTaper);
              BladeLength -= smoothstep(0.99, 1.0, InvCoronaDist);

        float BladeFeather = UIBladeFeather / delta(sqrt(InvDist - inv(UIBladeLength+0.1)));
              BladeFeather += -BladeFeather * smoothstep(0.7, 1.1, InvCoronaDist);

        float BladeResolve = lerp(BladeWidth, 1.0, BladeLength) * AngularDist * AngularDist;
              BladeResolve = pow(saturate(BladeResolve), BladeFeather);

        float BladeTipFade = rcp(pow(abs(1.0 + (Dist * UIBladeFadePow)), UIBladeFadeDist));

        BladeMask = BladeResolve * smoothstep(0.0, 0.05, InvDist - BladeSplayLen);
        BladeMask *= BladeTipFade * UIBladeInt;
    }


    [branch] if(UIEnableRays){
        float RandomRays = 0.0;

        float RayWidth = pow(abs(UIRaysWidth), 2.1);
        float2 PixelSize = rcp(ScreenSize.xy);
        static const float2 BFCoord[4] = {float2(0.5,0.5),float2(-0.5,0.5),
                                         float2(0.5,-0.5),float2(-0.5,-0.5)};
        [unroll] for(int i=0; i<4; ++i){
            float2 Coords = normalize(input.TexCoord.xy + BFCoord[i] * PixelSize);
            RandomRays += sqrt(Random::RandomSH(floor(Coords * RayWidth))) + 0.1;
        }
        RandomRays /= 4;

        float RayLength = lerp(0.1, UIRaysLength, input.SunInt.x);

        float CentreDist = min(0.0, -0.2 + round(input.SunInt.x - 0.2));
              CentreDist *= LinearStep(0.0, 0.1, Dist-0.05);

        float Rays = smoothstep(CentreDist, 1.0 - UIRaysVolume, CoronaDist);
              Rays = pow(saturate(InvDist), RandomRays * (7 / delta(Rays)));

        RayMask = smoothstep(RayLength, 0.1, CoronaDist);
        RayMask *= Rays * UIRaysInt * 1.8;
    }


    float Starburst = BladeMask * UIEnableBlades;

    Starburst *= LinearStep(0.2, 0.6, pow(input.SunInt.x, 2));
    Starburst += Depth * inv(SunBorder);
    Starburst += RayMask * LinearStep(0.3, 1.0, input.SunInt.x);
    Starburst *= input.SunInt.y;

	return float4(Starburst * input.Color.xyz * UIBurstInt * input.Color.w, 0.0);
}
#endif
/////////////////////////////////////////////////////////////////////////////////////////



//// Ghost Vertex Shader ////////////////////////////////////////////////////////////////

#ifdef GHOST_VERTEX_SHADER

cbuffer SunFlare : register(b2) {
    float4 Unused;
    float4 Unused2;
    float4 SunPositionUV;
};
Texture2D OcclusionLUT : register(t0);


GhostVSOutput main(VertexShaderInput input)
{
	GhostVSOutput output;
    output.Position = float4(input.Position.xy, 0.0, 1.0);
    output.TexCoord.xy = input.TexCoord.xy * 2.0 - 1.0;
    output.TexCoord.xy *= UIGhostCAFactor;

    output.CADispacement = float2(0,0);
    output.AtlasCoords = output.Color = float4(0,0,0,0);
    output.Vertices = (float2[10])0;

    output.SunInt = OcclusionLUT.Load(0);
    output.SunInt.x = LinearStep(0.4, 0.8, output.SunInt.w);

    float4 SunParams = OcclusionLUT.Load(int3(1,0,0));
    float Scale = Math::MapRange(SunParams.w, 0.02, 0.04, UIGhostScale * 0.8, UIGhostScale);
    output.Color.w = Math::MapRange(SunParams.w, 0.02, 0.04, 0.8, 1.0);

    [branch] if(output.SunInt.w > SUNCLIP){

        float2 SunPosition = SunPositionUV.xy * 2.0 - 1.0;
        float SunDist = length(SunPosition);

        float2x2 GhostRotation = DegreesToVector(UIGhostRotation).xyyx * float2(1.0, -1.0).xyxx;

        float GhostScale =  UIGhostSize * Scale * UIGhostCAFactor;
        float RadialDelta = mad(SunDist, -0.25, 1.0);

        float2 ClampPosition = lerp(float2(UIGhostClampOffset, -1.0), -SunPosition, SunDist);
               ClampPosition *= rsqrt(delta(dot(ClampPosition, ClampPosition))) * RadialDelta;

        float2 Bind = normalize(-SunPosition) * Math::nRoot(SunDist*2, UIGhostMoveCurve);

        float2 GhostPosition = lerp(Bind, ClampPosition, UIGhostClampEnable);
               GhostPosition = mad(GhostPosition - SunPosition, UIGhostOffset, SunPosition);


        output.Position.xy = mul(GhostRotation, input.Position.xy);
        output.Position.y *= ScreenSize.z;
        output.Position.xy = mad(output.Position.xy, GhostScale, GhostPosition);

        [unroll] for(int i=0; i<9; ++i)
            output.Vertices[i] = DegreesToVector(360.0 / UIGhostShape * i);
        output.Vertices[9] = output.Vertices[0];

        float2 AtlasCoords = (Random::RandomSH(UIGhostOffset * 10.0) * 0.5 + 0.5) * UIGhostAtlas.z * output.TexCoord.xy * 0.5 + 0.5;
        output.AtlasCoords = float4(CoordMath::AtlasFetch4x4(AtlasCoords, UIGhostAtlas.x), UIGhostAtlas.y, 0);

        float2 CADispacement = normalize(SunPosition - GhostPosition) * float2(1.0, -1.0);
        output.CADispacement = mul(GhostRotation, CADispacement * (UIGhostCAFactor - 1.0) * 0.5);

        output.Color.xyz = lerp(SunParams.xyz, UIColor.xyz, UIGhostSat);
    }

	output.Position = float4(output.Position.xy, 0.0, 1.0);

    return output;
}
#endif
/////////////////////////////////////////////////////////////////////////////////////////



//// Ghost Pixel Shader /////////////////////////////////////////////////////////////////

#ifdef GHOST_PIXEL_SHADER

Texture2D AtlasTexture : register(t1);


float4 main(GhostVSOutput input) : SV_Target
{
    clip(input.SunInt.w - SUNCLIP);

    float2 GhostOffset[3];
    float3 Ghost;

    [unroll] for(int x=0; x<3; ++x){
        GhostOffset[x] = input.TexCoord.xy + (input.CADispacement * (x-1));
        Ghost[x] = saturate(inv(length(GhostOffset[x])));
    }
    clip(Math::min3(Ghost));


	[loop] for(int i = 0; i < UIGhostShape; ++i){
        float2 Edge = (input.Vertices[i+1] - input.Vertices[i]) * float2(-1.0, 1.0);

        [unroll] for(int j=0; j<3; ++j){
            float Local = dot(Edge.yx, input.Vertices[i] - GhostOffset[j]);
            float EdgeDist = lerp(Local, Ghost[j], inv(Local * Local) * UIGhostRoundness);

            Ghost[j] = min(Ghost[j], EdgeDist);
        }
    }
    clip(Math::max3(Ghost));

    float NSGhost = abs(Math::max3(Ghost));

    Ghost = smoothstep(-0.05, UIGhostFeather, Ghost);

    float EdgeEffect = mad(0.5, exp(-5 * NSGhost), exp(-100 * NSGhost)) * 0.5;
          EdgeEffect = LinearStep(0.0, 0.6, EdgeEffect);

    float3 Color = Ghost * input.Color.xyz;
           Color *= lerp(UIGhostInsideInt, 1.5, EdgeEffect);

    float3 Atlas = AtlasTexture.Sample(Linear_Sampler, input.AtlasCoords.xy).xyz;
    Color += Atlas * input.AtlasCoords.z * inv(EdgeEffect) * 0.5;

    Color *= UIGhostInt * UIColor.w * input.Color.w * input.SunInt.x;

    return float4(Color, 1.0);

}

#endif
/////////////////////////////////////////////////////////////////////////////////////////



//// Sun Glare Vertex Shader ////////////////////////////////////////////////////////////

#ifdef SUNGLARE_VERTEX_SHADER

cbuffer SunFlare : register(b2) {
    float4 Unused;
    float4 Unused2;
    float4 SunPositionUV;
};
Texture2D OcclusionLUT : register(t0);


SunGlareVertexOutput main(VertexShaderInput input)
{
    SunGlareVertexOutput output;
    output.TexCoord = input.TexCoord.xyxy;
    output.TexCoord.zw = output.TexCoord.xy * 2.0 - 1.0;

    float SunRadius = OcclusionLUT.Load(int3(1,0,0)).w;

    float GlareScale = Math::MapRange(SunRadius, 0.02, 0.055, UISunGlareScale * 0.99, UISunGlareScale);

    output.Scale = GlareScale / UISunGlareScale;
    output.Scale = lerp(output.Scale*0.5, output.Scale, LinearStep(0.025, 0.055, SunRadius));

    output.Position.xy = mad(input.Position.xy * float2(1.0, ScreenSize.z), GlareScale, SunPositionUV.xy * 2.0 - 1.0);
    output.Position = float4(output.Position.xy, 0.0, 1.0);

    output.SunInt = OcclusionLUT.Load(0);
    output.SkyColor = OcclusionLUT.Load(int3(2,0,0));
    output.Color = UISunGlareColor.xyz;


    return output;
}
#endif
/////////////////////////////////////////////////////////////////////////////////////////



//// Sun Glare Pixel Shader /////////////////////////////////////////////////////////////

#ifdef SUNGLARE_PIXEL_SHADER

Texture2D Depth : register(t0);
Texture2D Main : register(t1);

float4 main(SunGlareVertexOutput input) : SV_Target
{
    float Dist = length(input.TexCoord.zw);
    float InvDist = inv(Dist);
    clip(InvDist);

    float Intensity = UISunGlareInt;
          Intensity *= input.Scale * 0.8;

    float sigma = max(0.01, Intensity);
    float Glow = exp(-(Dist * Dist) / (2.0 * sigma * sigma));
          Glow *= pow(saturate(InvDist), Intensity * 2) * Intensity;

    float3 Scene = Main.Sample(Point_Sampler, input.Position.xy / ScreenSize.xy).xyz;

    float Edge = UISunGlareOuterInt * Dist;

    float3 Color = float3(1.0, 1.0, 1.0) * Glow + (input.Color * Edge) - Scene;
    Color *= smoothstep(-0.01, UISunGlareFade, InvDist) * InvDist;

    float depth = Depth.SampleCmpLevelZero(Depth_Sampler, input.Position.xy / ScreenSize.xy, 1).x;
          depth = saturate(depth + input.SunInt.x);

    Color *= depth * input.SunInt.y;

    return float4(Color, 0.0);
}
#endif
/////////////////////////////////////////////////////////////////////////////////////////



//// Halo Vertex Shader /////////////////////////////////////////////////////////////////

#ifdef HALO_VERTEX_SHADER

cbuffer SunFlare : register(b2) {
    float4 Unused;
    float4 Unused2;
    float4 SunPositionUV;
};
Texture2D OcclusionLUT : register(t0);


HaloVertexOutput main(VertexShaderInput input)
{
    HaloVertexOutput output;
    output.TexCoord = input.TexCoord.xyxy * 2.0 - 1.0;
    output.Position = float4(0.0, 0.0, 0.0, 1.0);

    float4 SunParams = OcclusionLUT.Load(int3(1,0,0));
    output.SunColor = SunParams.xyz;
    output.SunInt = OcclusionLUT.Load(0);

    float Scale = Math::MapRange(SunParams.w, 0.02, 0.05, UIHaloScale * 0.8, UIHaloScale);
    output.SunInt.z *= Math::MapRange(SunParams.w, 0.02, 0.05, 0.8, 1.0);

    if (output.SunInt.w > SUNCLIP){
        float2 SunPosition = SunPositionUV.xy * 2.0 - 1.0;
        float Dist = length(SunPosition);

        float2x2 RotateOffset = DegreesToVector(-SunPosition.x * (360 * UIHaloRotationSpeed)).xyyx;

        output.Position.xy = mul(RotateOffset * float2(1.0, -1.0).xyxx, input.Position.xy);
        output.Position.y *= ScreenSize.z;

        float Flipped = (UIHaloFlipExpOffset) ? 1.0 - Dist : Dist;

        float ScaleOffset = lerp(UIHaloExpMinSize, UIHaloExpMaxSize * 5.0, saturate(Flipped));
              ScaleOffset = lerp(Scale, Scale * ScaleOffset, UIHaloEnableExp);

        output.Position = float4(mad(output.Position.xy, ScaleOffset, SunPosition), 0.0, 1.0);
    }

    return output;
}
#endif
/////////////////////////////////////////////////////////////////////////////////////////



//// Halo Pixel Shader //////////////////////////////////////////////////////////////////

#ifdef HALO_PIXEL_SHADER


float4 main(HaloVertexOutput input) : SV_Target
{
    clip(input.SunInt.w - SUNCLIP);

    float Radial = length(input.TexCoord.xy);
    float IDist = inv(UIHaloLength * 0.5);
    float ODist = 1.0;

    float Ring = max(Radial - ODist, -(Radial - IDist));

    clip(-Ring);

    float3 ColorShift = saturate(inv(abs(LinearStep(IDist, ODist, Radial) * 2 - float3(0, 1, 2))));

    float3 Color = lerp(UIHaloColor.xyz, ColorShift, UIHaloCrShift);

    float2 Polar = CoordMath::CartesianToPolar(input.TexCoord.xy);
    float DeltaStep = radians(UIHaloIncr);
    float HalfDelta = DeltaStep * 0.5;

    float DeltaMap = Math::ufmod(Polar.y + HalfDelta, DeltaStep) - HalfDelta; //+- Delta/2

    float Taper = min(Radial - IDist, ODist - Radial);
          Taper = saturate(Taper / delta(UIHaloTaper * UIHaloLength * 2.0));

    float RectifyMask = abs(Polar.x * sin(DeltaMap));

    RectifyMask = inv(smoothstep(0.0, UIHaloWidth * Taper, RectifyMask)) * UIHaloInt;
    RectifyMask *= LinearStep(0.4, 0.8, input.SunInt.w) * input.SunInt.z;

    return float4(Color * RectifyMask, 0.0);
}
#endif
/////////////////////////////////////////////////////////////////////////////////////////



//// Lens Glare Vertex Shader ///////////////////////////////////////////////////////////

#ifdef LENSGLARE_VERTEX_SHADER

cbuffer SunFlare : register(b2) {
    float4 Unused;
    float4 Unused2;
    float4 SunPositionUV;
};
Texture2D OcclusionLUT : register(t0);


LensGlareVertexOutput main(VertexShaderInput input)
{
	LensGlareVertexOutput output;
    output.TexCoord = input.TexCoord.xyxy * 2.0 - 1.0;

    float2 SunPosition = SunPositionUV.xy * 2.0 - 1.0;

    float angle = lerp(0.0, UIGlareMaxRot, -SunPosition.x) + 180.0;
    float2x2 GlareRotMat = DegreesToVector(angle).xyyx * float2(1.0, -1.0).xyxx;

    float2 GlarePos = float2(UIGlareXOffset, UIGlareYOffset) * 2.0 - 1.0;
           GlarePos = lerp(GlarePos + UIGlareScale, GlarePos - UIGlareScale, GlarePos * 0.5 + 0.5);

    output.Position.xy = mul(GlareRotMat, input.Position.xy);
    output.Position.xy = mad(output.Position.xy, UIGlareScale * float2(1.0, ScreenSize.z), GlarePos);

    output.SunInt = OcclusionLUT.Load(0);
    output.SunColor = OcclusionLUT.Load(int3(1,0,0)).xyz;

	output.Position = float4(output.Position.xy, 0.0, 1.0);

    return output;
}
#endif
/////////////////////////////////////////////////////////////////////////////////////////



//// Lens Glare Pixel Shader ////////////////////////////////////////////////////////////

#ifdef LENSGLARE_PIXEL_SHADER


float4 main(LensGlareVertexOutput input) : SV_Target
{

    float Mask = length(input.TexCoord.xy) - 0.9;
    float Mask2 = length(input.TexCoord.xy - float2(0.0, UIGlareCutDepth)) - UIGlareRadius;

    clip(min(-max(Mask, -Mask2), input.SunInt.w - SUNCLIP));

    float Glare = lerp(0.0, 0.6, abs(max(Mask, -Mask2))) * UIGlareInt;

    float GlareMask = inv(smoothstep(0.5, 1.0, distance(input.TexCoord.y, -UIGlareTipFade)));
          GlareMask *= Glare * input.SunInt.w;

    return float4(float3(1.0, 1.0, 1.0) * GlareMask, 0.0);
}
#endif
/////////////////////////////////////////////////////////////////////////////////////////



//// Chromatic Aberration ///////////////////////////////////////////////////////////////

#ifdef CHROMATIC_ABERRATION_PIXEL_SHADER

Texture2D Main : register(t1);
Texture2D MotionVector : register(t2);


float4 main(VertexShaderOutput input) : SV_Target
{
    float2 Coords = input.TexCoord.xy;
    float4 Aberration;

    float2 MotionVec = MotionVector.Sample(PointMirror_Sampler, Coords).xy;

    float Curve = saturate(length(MotionVec) - UICAThreshold);
          Curve *= UICAIntensity;

    float2 Offset = Curve * normalize(delta(MotionVec));
           Offset = min(UICAMaxOffset, Offset);

    [unroll] for(int i=0; i<3; ++i)
        Aberration[i] = Main.Sample(PointMirror_Sampler, Coords + Offset * (i-1))[i];
    Aberration.w = Main.Sample(PointMirror_Sampler, Coords).w;


    return Aberration;
}

#endif
/////////////////////////////////////////////////////////////////////////////////////////



//// Ice Vertex Shader //////////////////////////////////////////////////////////////////

#ifdef ICE_VERTEX_SHADER


IceVertexOutput main(VertexShaderInput input)
{
    IceVertexOutput output;

    output.TexCoord.xy = input.TexCoord.xy;
    output.TexCoord.zw = input.TexCoord * 2.0 - 1.0;

    output.Color = UIFrostColor.xyz * UIFrostInt;

    output.Position = float4(input.Position.xy, 0.0, 1.0);

    return output;
}
#endif
/////////////////////////////////////////////////////////////////////////////////////////



//// Ice Pixel Shader ///////////////////////////////////////////////////////////////////

#ifdef ICE_PIXEL_SHADER

Texture2D IceTexture : register(t0);
Texture2D Main : register(t1);
Texture2D MotionVector : register(t2);


float4 main(IceVertexOutput input) : SV_Target
{
    float Dist = length(input.TexCoord.zw);

    float3 Texture = IceTexture.Sample(Point_Sampler, input.TexCoord.xy).xyz;
    float FadeFactor = LinearStep(inv(SnowPrecipValue), 1.0, saturate(Dist-0.35));
    float3 Color = Texture * input.Color * FadeFactor;

    return float4(Color, 0.0);
}
#endif
/////////////////////////////////////////////////////////////////////////////////////////



//// Bypass VS //////////////////////////////////////////////////////////////////////////

#ifdef BYPASS_VERTEX_SHADER


VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    output.TexCoord = input.TexCoord;
    output.Position = float4(input.Position.xy, 0.0, 1.0);

    return output;
}
#endif
/////////////////////////////////////////////////////////////////////////////////////////


