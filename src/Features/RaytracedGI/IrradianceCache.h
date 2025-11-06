namespace IrradianceCache
{
	struct Data
	{
		float data;
	};

	struct SH1Data : Data
	{
		float4 shR;
		float4 shG;
		float4 shB;
	};

	struct SH2Data : Data
	{
		float4 sh0;
		float4 sh1;
		float4 sh2;
		float4 sh3;
		float4 sh4;
		float4 sh5;
		float4 sh6;
	};

	template <typename T>
	concept AllowedData = std::is_same_v<T, Data> || std::is_same_v<T, SH1Data> || std::is_same_v<T, SH2Data>;


	template <AllowedData T>
	struct Entry
	{
		float3 Position;
		T Irradiance;
	};
}