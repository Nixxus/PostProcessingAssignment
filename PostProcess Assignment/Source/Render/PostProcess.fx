//--------------------------------------------------------------------------------------
//	File: PostProcess.fx
//
//	Post processing shaders
//--------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------
// Global Variables
//--------------------------------------------------------------------------------------

// Post Process Area - Dimensions
float2 PPAreaTopLeft;     // Top-left and bottom-right coordinates of area to post process, provided as UVs into the scene texture...
float2 PPAreaBottomRight; // ... i.e. the X and Y coordinates range from 0.0 to 1.0 from left->right and top->bottom of viewport
float  PPAreaDepth;       // Depth buffer value for area (0.0 nearest to 1.0 furthest). Full screen post-processing uses 0.0f

// Other variables used for individual post-processes
float3 TintColour;
float3 VariableTintColour;
float2 NoiseScale;
float2 NoiseOffset;
float  DistortLevel;
float  BurnLevel;
float  SpiralTimer;
float  HeatHazeTimer;

// Texture maps
Texture2D SceneTexture;   // Texture containing the scene to copy to the full screen quad
Texture2D PostProcessMap; // Second map for special purpose textures used during post-processing

// Samplers to use with the above texture maps. Specifies texture filtering and addressing mode to use when accessing texture pixels
// Usually use point sampling for the scene texture (i.e. no bilinear/trilinear blending) since don't want to blur it in the copy process
SamplerState PointClamp
{
    Filter = MIN_MAG_MIP_POINT;
    AddressU = Clamp;
    AddressV = Clamp;
	MaxLOD = 0.0f;
};

// See comment above. However, screen distortions may benefit slightly from bilinear filtering (not tri-linear because we won't create mip-maps for the scene each frame)
SamplerState BilinearClamp
{
    Filter = MIN_MAG_LINEAR_MIP_POINT;
    AddressU = Clamp;
    AddressV = Clamp;
	MaxLOD = 0.0f;
};

// Use other filtering methods for the special purpose post-processing textures (e.g. the noise map)
SamplerState BilinearWrap
{
    Filter = MIN_MAG_LINEAR_MIP_POINT;
    AddressU = Wrap;
    AddressV = Wrap;
};
SamplerState TrilinearWrap
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = Wrap;
    AddressV = Wrap;
};


//--------------------------------------------------------------------------------------
// Structures
//--------------------------------------------------------------------------------------

// The full-screen and area post-processing vertex shader input uses a special input type, the vertex ID. This value is automatically generated and does
// not come from a vertex buffer. The value starts at 0 and increases by one with each vertex processed.
struct VS_POSTPROCESS_INPUT
{
    uint vertexId : SV_VertexID;
};

// Vertex shader output / pixel shader input for the post processing shaders
// Provides the viewport positions of the quad to be post processed, then *two* UVs. The Scene UVs indicate which part of the 
// scene texture is being post-processed. The Area UVs range from 0->1 within the area only - these UVs can be used to apply a
// second texture to the area itself, or to find the location of a pixel within the area affected (the Scene UVs could be
// used together with the dimensions variables above to calculate this 2nd set of UVs, but this way saves pixel shader work)
struct PS_POSTPROCESS_INPUT
{
    float4 ProjPos : SV_POSITION;
	float2 UVScene : TEXCOORD0;
	float2 UVArea  : TEXCOORD1;
};



//--------------------------------------------------------------------------------------
// Vertex Shaders
//--------------------------------------------------------------------------------------

// Post Process Full Screen and Area - Generate Vertices
//
// This rather unusual shader generates its own vertices - the input data is merely the vertex ID - an automatically generated increasing index.
// No vertex or index buffer required, so convenient on the C++ side. Probably not so efficient, but fine for just a few post-processing quads
PS_POSTPROCESS_INPUT PPQuad(VS_POSTPROCESS_INPUT vIn)
{
    PS_POSTPROCESS_INPUT vOut;
	
	// The four points of a full-screen quad - will use post process area dimensions (provided above) to scale these to the correct quad needed
	float2 Quad[4] =  { float2(0.0, 0.0),   // Top-left
	                    float2(1.0, 0.0),   // Top-right
	                    float2(0.0, 1.0),   // Bottom-left
	                    float2(1.0, 1.0) }; // Bottom-right

	// vOut.UVArea contains UVs for the area itself: (0,0) at top-left of area, (1,1) at bottom right. Simply the values stored in the Quad array above.
	vOut.UVArea = Quad[vIn.vertexId]; 

	// vOut.UVScene contains UVs for the section of the scene texture to use. The top-left and bottom-right coordinates are provided in the PPAreaTopLeft and
	// PPAreaBottomRight variables one pages above, use lerp to convert the Quad values above into appopriate coordinates (see AreaPostProcessing lab for detail)
	vOut.UVScene = lerp( PPAreaTopLeft, PPAreaBottomRight, vOut.UVArea ); 
	             
	// vOut.ProjPos contains the vertex positions of the quad to render, measured in viewport space here. The x and y are same as Scene UV coords but in range -1 to 1 (and flip y axis),
	// the z value takes the depth value provided for the area (PPAreaDepth) and a w component of 1 to prevent the perspective divide (already did that in the C++)
	vOut.ProjPos  = float4( vOut.UVScene * 2.0f - 1.0f, PPAreaDepth, 1.0f ); 
	vOut.ProjPos.y = -vOut.ProjPos.y;
	
    return vOut;
}


//--------------------------------------------------------------------------------------
// Post-processing Pixel Shaders
//--------------------------------------------------------------------------------------

// Post-processing shader that simply outputs the scene texture, i.e. no post-processing. A waste of processing, but illustrative
float4 PPCopyShader( PS_POSTPROCESS_INPUT ppIn ) : SV_Target
{
	float3 ppColour = SceneTexture.Sample( PointClamp, ppIn.UVScene );
	return float4( ppColour, 1.0f );
}


// Post-processing shader that tints the scene texture to a given colour
float4 PPTintShader( PS_POSTPROCESS_INPUT ppIn ) : SV_Target
{
	// Sample the texture colour (look at shader above) and multiply it with the tint colour (variables near top)
	float3 ppColour = SceneTexture.Sample( PointClamp, ppIn.UVScene ) * TintColour;
	return float4( ppColour, 1.0f );
}

float4 PPVariableTintShader( PS_POSTPROCESS_INPUT ppIn ) : SV_Target
{
	// Sample the texture colour (look at shader above) and multiply it with the tint colour (variables near top)
	float3 ppColour = SceneTexture.Sample( PointClamp, ppIn.UVScene ) * VariableTintColour;
	return float4( ppColour, 0.7f );
}

float4 PPHighContrastShader( PS_POSTPROCESS_INPUT ppIn ) : SV_Target
{
	float3 ppColour = SceneTexture.Sample( PointClamp, ppIn.UVScene );

	float total = ppColour.r + ppColour.g + ppColour.g;

	/*if (total > 1.5f)
	{
		ppColour.r += (1 - ppColour.r)/3;
		ppColour.g += (1 - ppColour.g)/3;
		ppColour.b += (1 - ppColour.b)/3;
	}
	else
	{
		ppColour.r /=3;
		ppColour.g /=3;
		ppColour.b /=3;
	}*/

	if (ppColour.r > 0.5f)
	{
		ppColour.r += (1 - ppColour.r)/4;
	}
	else
	{
		ppColour.r /=5.0f;
	}

	if (ppColour.g > 0.5f)
	{
		ppColour.g += (1 - ppColour.g)/4;
	}
	else
	{
		ppColour.g /=5.0f;
	}

	if (ppColour.b > 0.5f)
	{
		ppColour.b += (1 - ppColour.b)/4;
	}
	else
	{
		ppColour.b /= 5.0f;
	}


	return float4( ppColour, 1.0f );
}

float4 PPUnderwaterShader( PS_POSTPROCESS_INPUT ppIn ) : SV_Target
{
	const float EffectStrength = 0.02f;
	
	// Calculate alpha to display the effect in a softened circle, could use a texture rather than calculations for the same task.
	// Uses the second set of area texture coordinates, which range from (0,0) to (1,1) over the area being processed
	const float softEdge = 0.15f; // Softness of the edge of the circle - range 0.001 (hard edge) to 0.25 (very soft)
	float2 centreVector = ppIn.UVArea - float2(0.5f, 0.5f);
	float centreLengthSq = dot(centreVector, centreVector);
	float ppAlpha = 1.0f - saturate( (centreLengthSq - 0.25f + softEdge) / softEdge ); // Soft circle calculation based on fact that this circle has a radius of 0.5 (as area UVs go from 0->1)

	// Haze is a combination of sine waves in x and y dimensions
	float SinX = sin(ppIn.UVArea.x * radians(1440.0f) + HeatHazeTimer);
	float SinY = sin(ppIn.UVArea.y * radians(3600.0f) + HeatHazeTimer * 0.7f);
	
	// Offset for scene texture UV based on haze effect
	// Adjust size of UV offset based on the constant EffectStrength, the overall size of area being processed, and the alpha value calculated above
	float2 hazeOffset = float2(SinY, SinX) * EffectStrength * ppAlpha * (PPAreaBottomRight.xy - PPAreaTopLeft.xy);

	//tint pixel blue/green
	float3 UnderwaterTintColour = {0.1f,0.1f,0.9f};

	// Get pixel from scene texture, offset using haze and tint from tine colour
    float3 ppColour = SceneTexture.Sample( BilinearClamp, ppIn.UVScene + hazeOffset ) * UnderwaterTintColour;

	return float4( ppColour, 0.7f );
}

// Post-processing shader that tints the scene texture to a given colour
float4 PPGreyNoiseShader( PS_POSTPROCESS_INPUT ppIn ) : SV_Target
{
	const float NoiseStrength = 0.5f; // How noticable the noise is

	// Get texture colour, and average r, g & b to get a single grey value
    float3 texColour = SceneTexture.Sample( PointClamp, ppIn.UVScene );
    float grey = (texColour.r + texColour.g + texColour.b) / 3.0f;
    
    // Get noise UV by scaling and offseting texture UV. Scaling adjusts how fine the noise is.
    // The offset is randomised to give a constantly changing noise effect (like tv static)
    float2 noiseUV = ppIn.UVArea * NoiseScale + NoiseOffset;
    grey += NoiseStrength * (PostProcessMap.Sample( BilinearWrap, noiseUV ).r - 0.5f); // Noise can increase or decrease grey value
    float3 ppColour = grey;

	// Calculate alpha to display the effect in a softened circle, could use a texture rather than calculations for the same task.
	// Uses the second set of area texture coordinates, which range from (0,0) to (1,1) over the area being processed
	float softEdge = 0.05f; // Softness of the edge of the circle - range 0.001 (hard edge) to 0.25 (very soft)
	float2 centreVector = ppIn.UVArea - float2(0.5f, 0.5f);
	float centreLengthSq = dot(centreVector, centreVector);
	float ppAlpha = 1.0f - saturate( (centreLengthSq - 0.25f + softEdge) / softEdge ); // Soft circle calculation based on fact that this circle has a radius of 0.5 (as area UVs go from 0->1)

    // Output final colour
	return float4( ppColour, ppAlpha );
}


// Post-processing shader that "burns" the image away
float4 PPBurnShader( PS_POSTPROCESS_INPUT ppIn ) : SV_Target
{
	const float4 White = 1.0f;
	
	// Pixels are burnt with these colours at the edges
	const float4 BurnColour = float4(0.8f, 0.4f, 0.0f, 1.0f);
	const float4 GlowColour = float4(1.0f, 0.8f, 0.0f, 1.0f);
	const float GlowAmount = 0.15f; // Thickness of glowing area
	const float Crinkle = 0.1f; // Amount of texture crinkle at the edges 

	// Get burn texture colour
    float4 burnTexture = PostProcessMap.Sample( TrilinearWrap, ppIn.UVArea );
    
    // The range of burning colours are from BurnLevel  to BurnLevelMax
	float BurnLevelMax = BurnLevel + GlowAmount; 

    // Output black when current burn texture value below burning range
    if (burnTexture.r <= BurnLevel)
    {
		return float4( 0.0f, 0.0f, 0.0f, 1.0f );
	}
    
    // Output scene texture untouched when current burnTexture texture value above burning range
	else if (burnTexture.r >= BurnLevelMax)
    {
		float3 ppColour = SceneTexture.Sample( PointClamp, ppIn.UVScene );
		return float4( ppColour, 1.0f );
	}
	
	else // Draw burning edges
	{
		float3 ppColour;

		// Get level of glow (0 = none, 1 = max)
		float GlowLevel = 1.0f - (burnTexture.r - BurnLevel) / GlowAmount;

		// Extract direction to crinkle (2D vector) from the g & b components of the burn texture sampled above (converting from 0->1 range to -0.5->0.5 range)
		float2 CrinkleVector = burnTexture.rg - float2(0.5f, 0.5f);
		
		// Get main texture colour using crinkle offset
	    float4 texColour =  SceneTexture.Sample( PointClamp, ppIn.UVScene - GlowLevel * Crinkle * CrinkleVector );

		// Split glow into two regions - the very edge and the inner section
		GlowLevel *= 2.0f;
		if (GlowLevel < 1.0f)
		{		
			// Blend from main texture colour on inside to burn tint in middle of burning area
			ppColour = lerp( texColour, BurnColour * texColour, GlowLevel );
		}
		else
		{
			// Blend from burn tint in middle of burning area to bright glow at the burning edges
			ppColour = lerp( BurnColour * texColour, GlowColour, GlowLevel - 1.0f );
		}
		return float4( ppColour, 1.0f );
	}
}


// Post-processing shader that distorts the scene as though viewed through cut glass
float4 PPDistortShader( PS_POSTPROCESS_INPUT ppIn ) : SV_Target
{
	const float LightStrength = 0.025f;
	
	// Get distort texture colour
    float4 distortTexture = PostProcessMap.Sample( TrilinearWrap, ppIn.UVArea );

	// Get direction (2D vector) to distort UVs from the g & b components of the distort texture (converting from 0->1 range to -0.5->0.5 range)
	float2 DistortVector = distortTexture.rg - float2(0.5f, 0.5f);
			
	// Simple fake diffuse lighting formula based on 2D vector, light coming from top-left
	float light = dot( normalize(DistortVector), float2(0.707f, 0.707f) ) * LightStrength;
	
	// Get final colour by adding fake light colour plus scene texture sampled with distort texture offset
	float3 ppColour = light + SceneTexture.Sample( BilinearClamp, ppIn.UVScene + DistortLevel * DistortVector );

    return float4( ppColour, 1.0f );
}


// Post-processing shader that spins the area in a vortex
float4 PPSpiralShader( PS_POSTPROCESS_INPUT ppIn ) : SV_Target
{
	// Get vector from UV at centre of post-processing area to UV at pixel
	const float2 centreUV = (PPAreaBottomRight.xy + PPAreaTopLeft.xy) / 2.0f;
	float2 centreOffsetUV = ppIn.UVScene - centreUV;
	float centreDistance = length( centreOffsetUV ); // Distance of pixel from UV (i.e. screen) centre
	
	// Get sin and cos of spiral amount, increasing with distance from centre
	float s, c;
	sincos( centreDistance * SpiralTimer * SpiralTimer, s, c );
	
	// Create a (2D) rotation matrix and apply to the vector - i.e. rotate the
	// vector around the centre by the spiral amount
	matrix<float,2,2> rot2D = { c, s,
	                           -s, c };
	float2 rotOffsetUV = mul( centreOffsetUV, rot2D );

	// Sample texture at new position (centre UV + rotated UV offset)
    float3 ppColour = SceneTexture.Sample( BilinearClamp, centreUV + rotOffsetUV );

	// Calculate alpha to display the effect in a softened circle, could use a texture rather than calculations for the same task.
	// Uses the second set of area texture coordinates, which range from (0,0) to (1,1) over the area being processed
	const float softEdge = 0.05f; // Softness of the edge of the circle - range 0.001 (hard edge) to 0.25 (very soft)
	float2 centreVector = ppIn.UVArea - float2(0.5f, 0.5f);
	float centreLengthSq = dot(centreVector, centreVector);
	float ppAlpha = 1.0f - saturate( (centreLengthSq - 0.25f + softEdge) / softEdge ); // Soft circle calculation based on fact that this circle has a radius of 0.5 (as area UVs go from 0->1)

    return float4( ppColour, ppAlpha );
}


// Post-processing shader that gives a semi-transparent wiggling heat haze effect
float4 PPHeatHazeShader( PS_POSTPROCESS_INPUT ppIn ) : SV_Target
{
	const float EffectStrength = 0.02f;
	
	// Calculate alpha to display the effect in a softened circle, could use a texture rather than calculations for the same task.
	// Uses the second set of area texture coordinates, which range from (0,0) to (1,1) over the area being processed
	const float softEdge = 0.15f; // Softness of the edge of the circle - range 0.001 (hard edge) to 0.25 (very soft)
	float2 centreVector = ppIn.UVArea - float2(0.5f, 0.5f);
	float centreLengthSq = dot(centreVector, centreVector);
	float ppAlpha = 1.0f - saturate( (centreLengthSq - 0.25f + softEdge) / softEdge ); // Soft circle calculation based on fact that this circle has a radius of 0.5 (as area UVs go from 0->1)

	// Haze is a combination of sine waves in x and y dimensions
	float SinX = sin(ppIn.UVArea.x * radians(1440.0f) + HeatHazeTimer);
	float SinY = sin(ppIn.UVArea.y * radians(3600.0f) + HeatHazeTimer * 0.7f);
	
	// Offset for scene texture UV based on haze effect
	// Adjust size of UV offset based on the constant EffectStrength, the overall size of area being processed, and the alpha value calculated above
	float2 hazeOffset = float2(SinY, SinX) * EffectStrength * ppAlpha * (PPAreaBottomRight.xy - PPAreaTopLeft.xy);

	// Get pixel from scene texture, offset using haze
    float3 ppColour = SceneTexture.Sample( BilinearClamp, ppIn.UVScene + hazeOffset );

	// Adjust alpha on a sine wave - better to have it nearer to 1.0 (but don't allow it to exceed 1.0)
    ppAlpha *= saturate(SinX * SinY * 0.33f + 0.55f);

	return float4( ppColour, ppAlpha );
}


//--------------------------------------------------------------------------------------
// States
//--------------------------------------------------------------------------------------

RasterizerState CullBack  // Cull back facing polygons - post-processing quads should be oriented facing the camera
{
	CullMode = None;
};
RasterizerState CullNone  // Cull none of the polygons, i.e. show both sides
{
	CullMode = None;
};

DepthStencilState DepthWritesOn  // Write to the depth buffer - normal behaviour 
{
	DepthWriteMask = ALL;
};
DepthStencilState DepthWritesOff // Don't write to the depth buffer, but do read from it - useful for area based post-processing. Full screen post-process is given 0 depth, area post-processes
{                                // given a valid depth in the scene. Post-processes will not obsucre each other (in particular full-screen will not obscure area), but sorting issues may remain
	DepthWriteMask = ZERO;
};
DepthStencilState DisableDepth   // Disable depth buffer entirely
{
	DepthFunc      = ALWAYS;
	DepthWriteMask = ZERO;
};

BlendState NoBlending // Switch off blending - pixels will be opaque
{
    BlendEnable[0] = FALSE;
};
BlendState AlphaBlending
{
    BlendEnable[0] = TRUE;
    SrcBlend = SRC_ALPHA;
    DestBlend = INV_SRC_ALPHA;
    BlendOp = ADD;
};


//--------------------------------------------------------------------------------------
// Post Processing Techniques
//--------------------------------------------------------------------------------------

// Simple copy technique - no post-processing (pointless but illustrative)
technique10 PPCopy
{
    pass P0
    {
        SetVertexShader( CompileShader( vs_4_0, PPQuad() ) );
        SetGeometryShader( NULL );                                   
        SetPixelShader( CompileShader( ps_4_0, PPCopyShader() ) );

		SetBlendState( NoBlending, float4( 0.0f, 0.0f, 0.0f, 0.0f ), 0xFFFFFFFF );
		SetRasterizerState( CullBack ); 
		SetDepthStencilState( DepthWritesOff, 0 );
     }
}


// Tint the scene to a colour
technique10 PPTint
{
    pass P0
    {
        SetVertexShader( CompileShader( vs_4_0, PPQuad() ) );
        SetGeometryShader( NULL );                                   
        SetPixelShader( CompileShader( ps_4_0, PPTintShader() ) );

		SetBlendState( NoBlending, float4( 0.0f, 0.0f, 0.0f, 0.0f ), 0xFFFFFFFF );
		SetRasterizerState( CullBack ); 
		SetDepthStencilState( DepthWritesOff, 0 );
     }
}

technique10 PPVariableTint
{
    pass P0
    {
        SetVertexShader( CompileShader( vs_4_0, PPQuad() ) );
        SetGeometryShader( NULL );                                   
        SetPixelShader( CompileShader( ps_4_0, PPVariableTintShader() ) );

		SetBlendState( AlphaBlending, float4( 0.0f, 0.0f, 0.0f, 0.0f ), 0xFFFFFFFF );
		SetRasterizerState( CullBack ); 
		SetDepthStencilState( DepthWritesOff, 0 );
     }
}

technique10 PPHighContrast
{
    pass P0
    {
        SetVertexShader( CompileShader( vs_4_0, PPQuad() ) );
        SetGeometryShader( NULL );                                   
        SetPixelShader( CompileShader( ps_4_0, PPHighContrastShader() ) );

		SetBlendState( AlphaBlending, float4( 0.0f, 0.0f, 0.0f, 0.0f ), 0xFFFFFFFF );
		SetRasterizerState( CullBack ); 
		SetDepthStencilState( DepthWritesOff, 0 );
     }
}

// Turn the scene greyscale and add some animated noise
technique10 PPGreyNoise
{
    pass P0
    {
        SetVertexShader( CompileShader( vs_4_0, PPQuad() ) );
        SetGeometryShader( NULL );                                   
        SetPixelShader( CompileShader( ps_4_0, PPGreyNoiseShader() ) );

		SetBlendState( AlphaBlending, float4( 0.0f, 0.0f, 0.0f, 0.0f ), 0xFFFFFFFF );
		SetRasterizerState( CullBack ); 
		SetDepthStencilState( DepthWritesOff, 0 );
     }
}

// Burn the scene away
technique10 PPBurn
{
    pass P0
    {
        SetVertexShader( CompileShader( vs_4_0, PPQuad() ) );
        SetGeometryShader( NULL );                                   
        SetPixelShader( CompileShader( ps_4_0, PPBurnShader() ) );

		SetBlendState( NoBlending, float4( 0.0f, 0.0f, 0.0f, 0.0f ), 0xFFFFFFFF );
		SetRasterizerState( CullBack ); 
		SetDepthStencilState( DepthWritesOff, 0 );
     }
}

// Distort the scene as though viewed through cut glass
technique10 PPDistort
{
    pass P0
    {
        SetVertexShader( CompileShader( vs_4_0, PPQuad() ) );
        SetGeometryShader( NULL );                                   
        SetPixelShader( CompileShader( ps_4_0, PPDistortShader() ) );

		SetBlendState( NoBlending, float4( 0.0f, 0.0f, 0.0f, 0.0f ), 0xFFFFFFFF );
		SetRasterizerState( CullBack ); 
		SetDepthStencilState( DepthWritesOff, 0 );
     }
}

// Spin the image in a vortex
technique10 PPSpiral
{
    pass P0
    {
        SetVertexShader( CompileShader( vs_4_0, PPQuad() ) );
        SetGeometryShader( NULL );                                   
        SetPixelShader( CompileShader( ps_4_0, PPSpiralShader() ) );

		SetBlendState( AlphaBlending, float4( 0.0f, 0.0f, 0.0f, 0.0f ), 0xFFFFFFFF );
		SetRasterizerState( CullBack ); 
		SetDepthStencilState( DepthWritesOff, 0 );
     }
}

// Wiggling alpha blending to create a heat haze effect
technique10 PPHeatHaze
{
    pass P0
    {
        SetVertexShader( CompileShader( vs_4_0, PPQuad() ) );
        SetGeometryShader( NULL );                                   
        SetPixelShader( CompileShader( ps_4_0, PPHeatHazeShader() ) );

		SetBlendState( AlphaBlending, float4( 0.0f, 0.0f, 0.0f, 0.0f ), 0xFFFFFFFF );
		SetRasterizerState( CullBack ); 
		SetDepthStencilState( DepthWritesOff, 0 );
     }
}

technique10 PPUnderwater
{
    pass P0
    {
        SetVertexShader( CompileShader( vs_4_0, PPQuad() ) );
        SetGeometryShader( NULL );                                   
        SetPixelShader( CompileShader( ps_4_0, PPUnderwaterShader() ) );

		SetBlendState( AlphaBlending, float4( 0.0f, 0.0f, 0.0f, 0.0f ), 0xFFFFFFFF );
		SetRasterizerState( CullBack ); 
		SetDepthStencilState( DepthWritesOff, 0 );
     }
}
