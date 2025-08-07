namespace ExtendedTranslucency
{
	namespace MaterialModel
	{
		static const uint Default = 0;  // Use ExtendedTranslucencySettings
		static const uint RimLight = 1;
		static const uint IsotropicFabric = 2;
		static const uint AnisotropicFabric = 3;
		static const uint Disabled = 4;  // Any value >= 4
		// Yoyo this is a testing change to trigger CI shader validation
		static const uint Placeholder = 5; // Placeholder for future use
	}

	bool IsValidMaterial(uint Material)
	{
		return Material > 0 && Material < MaterialModel::Disabled;
	}

	uint GetMaterialModelFromDescriptor(uint Descriptor)
	{
		// TerrainHelper : 6
		// ExtraFeatureDescriptor : 3
		return (Descriptor >> 6) & 7;
	}

	float GetViewDependentAlphaNaive(float alpha, float3 view, float3 normal)
	{
		return 1.0 - (1.0 - alpha) * dot(view, normal);
	}

	float GetViewDependentAlphaFabric1D(float alpha, float3 view, float3 normal)
	{
		return alpha / min(1.0, (abs(dot(view, normal)) + 0.001));
	}

	float GetViewDependentAlphaFabric2D(float alpha, float3 view, float3x3 tbnTr)
	{
		float3 t = tbnTr[0];
		float3 b = tbnTr[1];
		float3 n = tbnTr[2];
		float3 v = view;
		float a0 = 1 - sqrt(1.0 - alpha);
		return a0 * (length(cross(v, t)) + length(cross(v, b))) / (abs(dot(v, n)) + 0.001) - a0 * a0;
	}

	float SoftClamp(float alpha, float limit)
	{
		// soft clamp [alpha,1] and remap the transparency
		alpha = min(alpha, limit / (1 + exp(-4 * (alpha - limit * 0.5) / limit)));
		return saturate(alpha);
	}
}
