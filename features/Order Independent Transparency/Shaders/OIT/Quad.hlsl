/////////////////////////////////////////////////////////////////////////////////////////////
// Copyright 2017 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
/////////////////////////////////////////////////////////////////////////////////////////////

// Vertex shader
struct VS_OUTPUT
{
	float4 pos : SV_POSITION;
	float2 tex : TEX_COORD0;
};

VS_OUTPUT VSMain(float4 Input : SV_POSITION)
{
	VS_OUTPUT output;
	output.pos = Input;
	output.tex = (Input.xy * float2(1, -1) + 1.0f) / 2.0f;
	return output;
}
