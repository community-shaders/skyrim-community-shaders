#include "Common/FrameBuffer.hlsli"
#include "Common/Math.hlsli"
#include "Common/CoordMath.hlsli"
#include "Common/Random.hlsli"
#include "Common/Color.hlsli"
#include "Common/SharedData.hlsli"

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

struct SunGlareVertexOutput
{
    float4 Position : SV_POSITION;
    float4 TexCoord : TEXCOORD0;
    nointerpolation float4 SunInt : DATA0;
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
    float4 SunPosition;
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
    float UIGhostInsideInt;

    float UIGlareScale;
    float UIGlareInt;
    uint UIGlareDynXPos;
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
    uint UICAOnlyOffsetRed;

    float UIFrostInt;
    float SnowPrecipValue;

    float4 UIBurstColor;
    float4 UISunGlareColor;
    float4 UIHaloColor;
    float4 UIGhostColor;
    float4 UIGhostAtlas;
    float4 UIFrostColor;
};

cbuffer SunFlare : register(b2) {
    float4 Unused;
    float4 Unused2;
    float4 SunPositionUV;
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
inline float  delta(float x) {return max(x, EPSILON_DIVISION);}
inline float2 delta(float2 x) {return max(x, EPSILON_DIVISION);}
inline float  LinearStep(float x, float y, float z) {return Math::LinearStep(x, y, z);}
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

float GetTemporalAverage(float Value, uint idx){
    SunLUT[int2(idx, 0)] = Value.xxxx;
    float Sum = 0.0;
    [unroll] for(int i=0; i < nFrames; ++i)
        Sum += SunLUT.Load(int2(5+i, 0)).x;

    return Sum / nFrames;
}

float UpdateDepthFactor(float2 SunCoords, float SunRadius){
    float DepthFactor = 0.00001;
    [loop] for(int i=0; i<OCCLSAMPLES; ++i){
        float2 Offset = Math::sincos2(Random::RandomSH(i) * Math::TAU);
        Offset *= sqrt(Random::RandomSH(i+1)) * SunRadius;
        DepthFactor += DepthTexture.SampleCmpLevelZero(Depth_Sampler, SunCoords + Offset, 1).x;
    }
    DepthFactor = LinearStep(0.0, 0.9, DepthFactor / OCCLSAMPLES);

    return GetTemporalAverage(DepthFactor, Frame);
}

float UpdateCloudFactor(uint CloudCover, float SunVisibility){
    if(SunVisibility < 100) return 0;

    float History = SunLUT.Load(int2(4,0)).x;
    float CloudFactor = inv(saturate(CloudCover / SunVisibility));
          CloudFactor = lerp(CloudFactor, History, 0.90);
    SunLUT[int2(4, 0)] = CloudFactor.xxxx;

    return CloudFactor;
}

float GetWeatherFactor(float CloudFactor){
    float PrecipFactor = inv(LinearStep(0.2, 0.8, Precip));
    float WeatherFade = inv(WeatherBasedFadeout);
    CloudFactor = LinearStep(0.4, 0.9, CloudFactor);

    return PrecipFactor * CloudFactor * WeatherFade;
}

float GetVisibleArea(float2 SunSSCoords, float SunSSRadius){
    float EdgeDist = min(Math::min2(SunSSCoords), Math::min2(ScreenSize.xy - SunSSCoords)) + SunSSRadius;
    if(EdgeDist <= 0) return 0;

    float Segment = 0;
    float SunR2 = SunSSRadius * SunSSRadius;
    [branch] if(EdgeDist < SunSSRadius * 2.0){
        Segment = SunR2 * acos(EdgeDist / SunSSRadius - 1.0);
        Segment -= (EdgeDist - SunSSRadius) * sqrt(EdgeDist * (SunSSRadius * 2 - EdgeDist));
    }
    return Math::PI * SunR2 - Segment;
}


void main(MaskVSOutput input)
{
    [branch] if(!(int)input.Position.x)
    {
        float2 SunCoords = buffer[0].xy;
        if(CoordMath::ChebyshevDistance(SunCoords * 2.0 - 1.0) < 1.15)
        {
            float2 SunSSCoords = SunCoords * ScreenSize.xy;
            float SunSSRadius = SunPosition.w, SunUVRadius = SunSSRadius * rcp(ScreenSize.y);

            float CloudCover = float(SunLUT_AT.Load(int2(0,0)));
            float SunVisibility = GetVisibleArea(SunSSCoords, SunSSRadius);

            float CloudFactor = UpdateCloudFactor(CloudCover, SunVisibility);
            float DepthFactor = UpdateDepthFactor(FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(SunCoords), SunUVRadius);
            float WeatherFactor = GetWeatherFactor(CloudFactor);
            float DistFactor = SunVisibility / (Math::PI * (SunSSRadius * SunSSRadius));

            SunLUT[int2(0,0)] = float4(DepthFactor, WeatherFactor, DistFactor, DepthFactor * WeatherFactor * DistFactor);
            SunLUT[int2(1,0)] = float4(saturate(SunBlendColor.xyz + 0.3), SunUVRadius);
            SunLUT[int2(3,0)] = float4(SunSSCoords, SunSSRadius, SunSSRadius);
        }
        else{ SunLUT[int2(0,0)] = float4(0,0,0,0); }
        SunLUT_AT[int2(0,0)] = 0;
    }
}
#endif
/////////////////////////////////////////////////////////////////////////////////////////



//// Starburst Vertex Shader ////////////////////////////////////////////////////////////

#ifdef STARBURST_VERTEX_SHADER

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

    output.Position.xy = mad(output.Position.xy * float2(1.0, ScreenSize.z), BurstScale, SunPositionUV.xy * 2.0 - 1.0);

    [unroll] for(int i=0; i<16; ++i)
		output.ApertureBlades[i] = DegreesToVector(360.0 / UIBladeVerts * (i + 0.5) + UIBladeRotation);
	output.ApertureBlades[15] = output.ApertureBlades[0];

    output.SunInt = OcclusionLUT.Load(0);
    output.SunInt.y = LinearStep(0.4, 0.8, output.SunInt.y);

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
    float BladeMask = 0.0, RayMask = 0.0;

    [branch] if(UIEnableBlades){
        float2 Normal = normalize(input.TexCoord.xy);
        float InvCoronaDist = 1.0 - CoronaDist;
        float AngularDist = 1.0;

        [unroll] for(int i=0; i<UIBladeVerts; ++i)
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
            RandomRays += sqrt(Random::RandomSH(floor(Coords * RayWidth)));
        }
        RandomRays /= 4;

        float RayLength = lerp(0.1, UIRaysLength, input.SunInt.x);

        float Rays = smoothstep(-0.1 * (CoronaDist > 0), 1.0 - UIRaysVolume, CoronaDist);
              Rays = pow(saturate(InvDist), RandomRays * (6 / delta(Rays)));

        RayMask = smoothstep(RayLength, 0.1, CoronaDist);
        RayMask = RayMask * Rays * UIRaysInt;
    }


    float Starburst = BladeMask * UIEnableBlades;

    Starburst *= LinearStep(0.2, 0.6, pow(input.SunInt.x, 2));
    Starburst += RayMask * LinearStep(0.3, 1.0, input.SunInt.x);
    Starburst *= input.SunInt.y * input.SunInt.z;

	return float4(Starburst * UIBurstColor.xyz * UIBurstInt, 0.0);
}
#endif
/////////////////////////////////////////////////////////////////////////////////////////



//// Ghost Vertex Shader ////////////////////////////////////////////////////////////////

#ifdef GHOST_VERTEX_SHADER

Texture2D OcclusionLUT : register(t0);


GhostVSOutput main(VertexShaderInput input)
{
	GhostVSOutput output;
    output.CADispacement = float2(0,0);
    output.AtlasCoords = float4(0,0,0,0);
    output.Color = float4(0,0,0,0);
    output.Vertices = (float2[10])0;
    output.Position.xy = float2(0,0);

    output.TexCoord.xy = (input.TexCoord.xy * 2.0 - 1.0) * UIGhostCAFactor;
    output.SunInt = OcclusionLUT.Load(0);
    output.SunInt.x = LinearStep(0.4, 0.8, output.SunInt.y * output.SunInt.x) * output.SunInt.z;

    [branch] if(output.SunInt.w > SUNCLIP && UIGhostColor.w > 0.0)
    {
        float2 SunPosition = SunPositionUV.xy * 2.0 - 1.0;
        float SunDist = length(SunPosition);
        float4 SunColor_Radius = OcclusionLUT.Load(int3(1,0,0));

        float GhostScale = Math::MapRange(SunColor_Radius.w, 0.02, 0.04, UIGhostScale * 0.8, UIGhostScale);
              GhostScale *= UIGhostSize * UIGhostCAFactor;

        float2x2 GhostRotation = DegreesToVector(UIGhostRotation).xyyx * float2(1.0, -1.0).xyxx;
        float RDelta = mad(SunDist, -0.25, 1.0);

        float2 ClampPosition = lerp(float2(UIGhostClampOffset, -1.0), -SunPosition, SunDist);
               ClampPosition *= rsqrt(delta(dot(ClampPosition, ClampPosition))) * RDelta;

        float2 GhostPosition = (UIGhostClampEnable) ? ClampPosition : SunDist * normalize(-SunPosition);
               GhostPosition = mad(GhostPosition - SunPosition, UIGhostOffset, SunPosition);

        output.Position.xy = mul(GhostRotation, input.Position.xy) * float2(1.0, ScreenSize.z);
        output.Position.xy = mad(output.Position.xy, GhostScale, GhostPosition);

        [unroll] for(int i=0; i<9; ++i)
            output.Vertices[i] = DegreesToVector(360.0 / UIGhostShape * i);
        output.Vertices[9] = output.Vertices[0];

        float2 AtlasCoords = (Random::RandomSH(UIGhostOffset * 10.0) * 0.5 + 0.5) * UIGhostAtlas.z * output.TexCoord.xy;
        output.AtlasCoords = float4(CoordMath::AtlasFetch2x2(AtlasCoords * 0.5 + 0.5, UIGhostAtlas.x), UIGhostAtlas.y, 0);

        float2 CADispacement = normalize(SunPosition - GhostPosition) * float2(1.0, -1.0);
        output.CADispacement = mul(GhostRotation, CADispacement * (UIGhostCAFactor - 1.0) * 0.5);

        output.Color.xyz = lerp(SunColor_Radius.xyz, UIGhostColor.xyz, UIGhostSat);
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
    clip(min(input.SunInt.w - SUNCLIP, UIGhostColor.w - 0.01));

    float2 GhostOffset[3];
    float3 Ghost;

    [unroll] for(int x=0; x<3; ++x){
        GhostOffset[x] = input.TexCoord.xy + (input.CADispacement * (x-1));
        Ghost[x] = saturate(inv(length(GhostOffset[x])));
    }
    clip(Math::min3(Ghost));

	[loop] for(int i = 0; i < UIGhostShape; ++i){
        float2 Edge = ((input.Vertices[i+1] - input.Vertices[i]) * float2(-1.0, 1.0)).yx;
        [unroll] for(int j=0; j<3; ++j){
            float Local = dot(Edge, input.Vertices[i] - GhostOffset[j]);
            float EdgeDist = lerp(Local, Ghost[j], inv(Local * Local) * UIGhostRoundness);

            Ghost[j] = min(Ghost[j], EdgeDist);
        }
    }
    clip(Math::max3(Ghost));

    float NSGhost = abs(Math::max3(Ghost));

    float EdgeEffect = mad(0.5, exp(-5 * NSGhost), exp(-100 * NSGhost)) * 0.5;
          EdgeEffect = LinearStep(0.0, 0.6, EdgeEffect);

    Ghost = smoothstep(-0.05, UIGhostFeather, Ghost);

    float3 Color = Ghost * input.Color.xyz;
           Color *= lerp(UIGhostInsideInt, 1.5, EdgeEffect);

    float3 Atlas = AtlasTexture.Sample(Linear_Sampler, input.AtlasCoords.xy).xyz;
    Color += Atlas * input.AtlasCoords.z * inv(EdgeEffect) * 0.5;

    Color *= UIGhostInt * UIGhostColor.w * input.SunInt.x;

    return float4(Color, 1.0);

}
#endif
/////////////////////////////////////////////////////////////////////////////////////////



//// Halo Vertex Shader /////////////////////////////////////////////////////////////////

#ifdef HALO_VERTEX_SHADER

Texture2D OcclusionLUT : register(t0);


HaloVertexOutput main(VertexShaderInput input)
{
    HaloVertexOutput output;
    output.TexCoord = input.TexCoord.xyxy * 2.0 - 1.0;
    output.Position = float4(0.0, 0.0, 0.0, 1.0);

    float4 SunParams = OcclusionLUT.Load(int3(1,0,0));
    output.SunColor = SunParams.xyz;
    output.SunInt = OcclusionLUT.Load(0);
    output.SunInt.x *= output.SunInt.y;

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

    float3 ColorShift = saturate(1.0 - abs(LinearStep(IDist, ODist, Radial) * 2 - float3(0, 1, 2)));

    float3 Color = lerp(UIHaloColor.xyz, ColorShift, UIHaloCrShift);

    float2 Polar = CoordMath::CartesianToPolar(input.TexCoord.xy);
    float DeltaStep = radians(UIHaloIncr);
    float HalfDelta = DeltaStep * 0.5;

    float DeltaMap = Math::ufmod(Polar.y + HalfDelta, DeltaStep) - HalfDelta;

    float Taper = min(Radial - IDist, ODist - Radial);
          Taper = saturate(Taper / delta(UIHaloTaper * UIHaloLength * 2.0));

    float RectifyMask = abs(Polar.x * sin(DeltaMap));

    RectifyMask = inv(smoothstep(0.0, UIHaloWidth * Taper, RectifyMask)) * UIHaloInt;
    RectifyMask *= LinearStep(0.4, 0.8, input.SunInt.x) * input.SunInt.z;

    return float4(Color * RectifyMask, 0.0);
}
#endif
/////////////////////////////////////////////////////////////////////////////////////////



//// Lens Glare Vertex Shader ///////////////////////////////////////////////////////////

#ifdef LENSGLARE_VERTEX_SHADER

Texture2D OcclusionLUT : register(t0);


LensGlareVertexOutput main(VertexShaderInput input)
{
	LensGlareVertexOutput output;
    output.TexCoord = input.TexCoord.xyxy * 2.0 - 1.0;

    float2 SunPosition = SunPositionUV.xy * 2.0 - 1.0;

    float angle = lerp(0.0, UIGlareMaxRot, -SunPosition.x) + 180.0;
    float2x2 GlareRotMat = DegreesToVector(angle).xyyx * float2(1.0, -1.0).xyxx;

    float2 GlarePos = float2(UIGlareXOffset, UIGlareYOffset) * 2.0 - 1.0;

    [branch] if(UIGlareDynXPos) {
        float SunDist = length(SunPosition);
        float RadialDelta = mad(SunDist, -0.25, 1.0);
        float2 ClampPosition = lerp(float2(GlarePos.x, -1.0), -SunPosition, SunDist);
        GlarePos.x = ClampPosition.x * rsqrt(delta(dot(ClampPosition, ClampPosition))) * RadialDelta * ScreenSize.z;
    }

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
    float2 Coords = input.TexCoord.xy;
    float Mask = length(Coords) - 0.9;
    float Mask2 = length(Coords - float2(0.0, UIGlareCutDepth)) - UIGlareRadius;

    clip(min(-max(Mask, -Mask2), input.SunInt.w - SUNCLIP));

    float Glare = lerp(0.0, 0.6, abs(max(Mask, -Mask2))) * UIGlareInt;

    float GlareMask = inv(smoothstep(0.5, 1.0, distance(Coords.y, -UIGlareTipFade)));
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
    float4 Aberration = Main.Load(int3(input.Position.xy, 0));

    float2 Offset = UICAIntensity * (1.0 + 5 * (UICAThreshold == 0.0));
    if(UICAThreshold > 0.0){
        float2 Motion = MotionVector.Load(int3(input.Position.xy, 0)).xy - 1e-6;
        Offset *= saturate(length(Motion) - UICAThreshold);
        Offset = min(UICAMaxOffset.xx, Offset) * ScreenSize.xy * normalize(Motion);
    }

    [branch] if(UICAOnlyOffsetRed){
        Aberration.x = Main.Load(int3(clamp(int2(input.Position.xy) + int2(-Offset), int2(0,0), int2(ScreenSize.xy - 1)), 0)).x;
    }else{
        [unroll] for(int i=0; i<3; ++i){
            int2 Coords = int2(input.Position.xy) + int2(Offset * (i-1));
            Aberration[i] = Main.Load(int3(clamp(Coords, int2(0,0), int2(ScreenSize.xy - 1)), 0))[i];
        }
    }

    return Aberration;
}
#endif
/////////////////////////////////////////////////////////////////////////////////////////



//// Sun Glare Vertex Shader ////////////////////////////////////////////////////////////

#ifdef SUNGLARE_VERTEX_SHADER

Texture2D OcclusionLUT : register(t0);


SunGlareVertexOutput main(VertexShaderInput input)
{
    SunGlareVertexOutput output;
    output.TexCoord = input.TexCoord.xyxy;
    output.TexCoord.zw = output.TexCoord.xy * 2.0 - 1.0;

    output.Position.xy = mad(input.Position.xy * float2(1.0, ScreenSize.z), UISunGlareScale, SunPositionUV.xy * 2.0 - 1.0);
    output.Position = float4(output.Position.xy, 0.0, 1.0);

    output.SunInt = OcclusionLUT.Load(0);

    return output;
}
#endif
/////////////////////////////////////////////////////////////////////////////////////////



//// Sun Glare Pixel Shader /////////////////////////////////////////////////////////////

#ifdef SUNGLARE_PIXEL_SHADER

Texture2D DepthTexture : register(t0);
Texture2D SceneTexture : register(t1);


float4 main(SunGlareVertexOutput input) : SV_Target
{
    float Dist = length(input.TexCoord.zw);
    float InvDist = inv(Dist);
    clip(InvDist);

    float Intensity = UISunGlareInt;

    float sigma = max(0.01, Intensity);
    float Glow = exp(-(Dist * Dist) / (2.0 * sigma * sigma));
          Glow *= pow(saturate(InvDist), Intensity) * Intensity * 3.0;

    float2 SampleCoords = FrameBuffer::GetDynamicResolutionAdjustedScreenPosition(input.Position.xy / ScreenSize.xy);

    float Depth = DepthTexture.SampleCmpLevelZero(Depth_Sampler, SampleCoords, 1).x;
          Depth = saturate(Depth + input.SunInt.x);

    float Mask = Glow * smoothstep(0.0, UISunGlareFade, InvDist);

    float3 Color = UISunGlareColor.xyz * Mask * Depth * input.SunInt.y;

    return float4(Color, 1.0);
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
          FadeFactor *= LinearStep(0.0, 0.1, SnowPrecipValue);

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



