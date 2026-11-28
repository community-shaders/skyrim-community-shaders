// Converts generated blade counts into batched indirect draws. The VS discards the final batch's unused blades.
ByteAddressBuffer BladeArgs : register(t13);
RWByteAddressBuffer BatchArgs : register(u0);

[numthreads(2, 1, 1)] void main(uint segment : SV_GroupIndex) {
#if !defined(PGRASS_OUTER_LIST)
	if (segment != 0u)
		return;
#endif

	// Separate lanes keep FXC from eliminating the outer draw's stores during inlining.
	uint offset = segment * 20u;
	uint bladeCount = BladeArgs.Load(offset + 4u);
	BatchArgs.Store4(offset, uint4(BladeArgs.Load(offset), (bladeCount + BLADE_BATCH_SIZE - 1u) / BLADE_BATCH_SIZE, 0u, 0u));
	BatchArgs.Store(offset + 16u, 0u);
}
