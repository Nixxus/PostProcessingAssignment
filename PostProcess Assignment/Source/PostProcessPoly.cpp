/*******************************************
	PostProcessPoly.cpp

	Main scene and game functions
********************************************/

#include <Windows.h>
#include <sstream>
#include <string>
using namespace std;

#include <d3d10.h>
#include <d3dx10.h>

#include "Defines.h"
#include "CVector3.h"
#include "CVector4.h"
#include "Camera.h"
#include "Light.h"
#include "EntityManager.h"
#include "Messenger.h"
#include "CParseLevel.h"
#include "PostProcessPoly.h"

namespace gen
{

//*****************************************************************************
// Post-process data
//*****************************************************************************

// Enumeration of different post-processes
enum PostProcesses
{
	Copy, Tint, GreyNoise, Burn, Distort, Spiral, HeatHaze,
	NumPostProcesses
};

// Currently used post process
PostProcesses FullScreenFilter = Copy;

// Post-process settings
float BurnLevel = 0.0f;
const float BurnSpeed = 0.2f;
float SpiralTimer = 0.0f;
const float SpiralSpeed = 1.0f;
float HeatHazeTimer = 0.0f;
const float HeatHazeSpeed = 1.0f;

//Assignment Postprocess booleans.
bool VariableTint = false;
bool HighContrast = false;
bool Underwater	  = false;


// Separate effect file for full screen & area post-processes. Not necessary to use a separate file, but convenient given the architecture of this lab
ID3D10Effect* PPEffect;

// Technique name for each post-process
const string PPTechniqueNames[NumPostProcesses] = {	"PPCopy", "PPTint", "PPGreyNoise", "PPBurn", "PPDistort", "PPSpiral", "PPHeatHaze" };

// Technique pointers for each post-process
ID3D10EffectTechnique* PPTechniques[NumPostProcesses];
ID3D10EffectTechnique* PPVariableTintTechnique;
ID3D10EffectTechnique* PPHighContrastTechnique;
ID3D10EffectTechnique* PPUnderwaterTechnique;


// Will render the scene to a texture in a first pass, then copy that texture to the back buffer in a second post-processing pass
// So need a texture and two "views": a render target view (to render into the texture - 1st pass) and a shader resource view (use the rendered texture as a normal texture - 2nd pass)
ID3D10Texture2D*          SceneTexture = NULL;
ID3D10RenderTargetView*   SceneRenderTarget = NULL;
ID3D10ShaderResourceView* SceneShaderResource = NULL;

// Additional textures used by post-processes
ID3D10ShaderResourceView* NoiseMap = NULL;
ID3D10ShaderResourceView* BurnMap = NULL;
ID3D10ShaderResourceView* DistortMap = NULL;

// Variables to link C++ post-process textures to HLSL shader variables (for area / full-screen post-processing)
ID3D10EffectShaderResourceVariable* SceneTextureVar = NULL;
ID3D10EffectShaderResourceVariable* PostProcessMapVar = NULL; // Single shader variable used for the three maps above (noise, burn, distort). Only one is needed at a time

// Variables specifying the area used for post-processing
ID3D10EffectVectorVariable* PPAreaTopLeftVar = NULL;
ID3D10EffectVectorVariable* PPAreaBottomRightVar = NULL;
ID3D10EffectScalarVariable* PPAreaDepthVar = NULL;

// Other variables for individual post-processes
ID3D10EffectVectorVariable* TintColourVar = NULL;
ID3D10EffectVectorVariable* NoiseScaleVar = NULL;
ID3D10EffectVectorVariable* NoiseOffsetVar = NULL;
ID3D10EffectScalarVariable* DistortLevelVar = NULL;
ID3D10EffectScalarVariable* BurnLevelVar = NULL;
ID3D10EffectScalarVariable* SpiralTimerVar = NULL;
ID3D10EffectScalarVariable* HeatHazeTimerVar = NULL;

ID3D10EffectVectorVariable* VariableTintColourVar = NULL;


//*****************************************************************************


//-----------------------------------------------------------------------------
// Constants
//-----------------------------------------------------------------------------

// Control speed
const float CameraRotSpeed = 2.0f;
float CameraMoveSpeed = 80.0f;

// Amount of time to pass before calculating new average update time
const float UpdateTimePeriod = 0.25f;



//-----------------------------------------------------------------------------
// Global system variables
//-----------------------------------------------------------------------------

// Folders used for meshes/textures and effect files
extern const string MediaFolder;
extern const string ShaderFolder;

// Get reference to global DirectX variables from another source file
extern ID3D10Device*           g_pd3dDevice;
extern IDXGISwapChain*         SwapChain;
extern ID3D10DepthStencilView* DepthStencilView;
extern ID3D10RenderTargetView* BackBufferRenderTarget;
extern ID3DX10Font*            OSDFont;

// Actual viewport dimensions (fullscreen or windowed)
extern TUInt32 BackBufferWidth;
extern TUInt32 BackBufferHeight;

// Current mouse position
extern TUInt32 MouseX;
extern TUInt32 MouseY;

// Messenger class for sending messages to and between entities
extern CMessenger Messenger;


//-----------------------------------------------------------------------------
// Global game/scene variables
//-----------------------------------------------------------------------------

// Entity manager and level parser
CEntityManager EntityManager;
CParseLevel LevelParser( &EntityManager );

// Other scene elements
const int NumLights = 2;
CLight*  Lights[NumLights];
CCamera* MainCamera;
int WallUID;
float TintHue = 0; // stores the current hue of the fullscreen tint

// Sum of recent update times and number of times in the sum - used to calculate
// average over a given time period
float SumUpdateTimes = 0.0f;
int NumUpdateTimes = 0;
float AverageUpdateTime = -1.0f; // Invalid value at first



//-----------------------------------------------------------------------------
// Game Constants
//-----------------------------------------------------------------------------

// Lighting
const SColourRGBA AmbientColour( 0.3f, 0.3f, 0.4f, 1.0f );
CVector3 LightCentre( 0.0f, 30.0f, 50.0f );
const float LightOrbit = 170.0f;
const float LightOrbitSpeed = 0.2f;


//-----------------------------------------------------------------------------
// Scene management
//-----------------------------------------------------------------------------

// Creates the scene geometry
bool SceneSetup()
{
	// Prepare render methods
	InitialiseMethods();
	
	// Read templates and entities from XML file
	if (!LevelParser.ParseFile( "Entities.xml" )) return false;
	
	// Set camera position and clip planes
	MainCamera = new CCamera( CVector3( 25, 30, -115 ), CVector3(ToRadians(8.0f), ToRadians(-35.0f), 0) );
	MainCamera->SetNearFarClip( 2.0f, 300000.0f ); 

	//Spawn additional models
	EntityManager.CreateTemplate("Wall", "Wall1", "Wall1.x");
	WallUID = EntityManager.CreateEntity("Wall1");
	EntityManager.GetEntity(WallUID)->Matrix().SetScale(100);

	// Sunlight
	Lights[0] = new CLight( CVector3( -10000.0f, 6000.0f, 0000.0f), SColourRGBA(1.0f, 0.8f, 0.6f) * 12000, 20000.0f ); // Colour is multiplied by light brightness

	// Light orbiting area
	Lights[1] = new CLight( LightCentre, SColourRGBA(0.0f, 0.2f, 1.0f) * 50, 100.0f );

	return true;
}


// Release everything in the scene
void SceneShutdown()
{
	// Release render methods
	ReleaseMethods();

	// Release lights
	for (int light = NumLights - 1; light >= 0; --light)
	{
		delete Lights[light];
	}

	// Release camera
	delete MainCamera;

	// Destroy all entities
	EntityManager.DestroyAllEntities();
	EntityManager.DestroyAllTemplates();
}


//*****************************************************************************
// Post Processing Setup
//*****************************************************************************

// Prepare resources required for the post-processing pass
bool PostProcessSetup()
{
	// Create the "scene texture" - the texture into which the scene will be rendered in the first pass
	D3D10_TEXTURE2D_DESC textureDesc;
	textureDesc.Width  = BackBufferWidth;  // Match views to viewport size
	textureDesc.Height = BackBufferHeight;
	textureDesc.MipLevels = 1; // No mip-maps when rendering to textures (or we will have to render every level)
	textureDesc.ArraySize = 1;
	textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // RGBA texture (8-bits each)
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Usage = D3D10_USAGE_DEFAULT;
	textureDesc.BindFlags = D3D10_BIND_RENDER_TARGET | D3D10_BIND_SHADER_RESOURCE; // Indicate we will use texture as render target, and pass it to shaders
	textureDesc.CPUAccessFlags = 0;
	textureDesc.MiscFlags = 0;
	if (FAILED(g_pd3dDevice->CreateTexture2D( &textureDesc, NULL, &SceneTexture ))) return false;

	// Get a "view" of the texture as a render target - giving us an interface for rendering to the texture
	if (FAILED(g_pd3dDevice->CreateRenderTargetView( SceneTexture, NULL, &SceneRenderTarget ))) return false;

	// And get a shader-resource "view" - giving us an interface for passing the texture to shaders
	D3D10_SHADER_RESOURCE_VIEW_DESC srDesc;
	srDesc.Format = textureDesc.Format;
	srDesc.ViewDimension = D3D10_SRV_DIMENSION_TEXTURE2D;
	srDesc.Texture2D.MostDetailedMip = 0;
	srDesc.Texture2D.MipLevels = 1;
	if (FAILED(g_pd3dDevice->CreateShaderResourceView( SceneTexture, &srDesc, &SceneShaderResource ))) return false;
	
	// Load post-processing support textures
	if (FAILED( D3DX10CreateShaderResourceViewFromFile( g_pd3dDevice, (MediaFolder + "Noise.png").c_str() ,   NULL, NULL, &NoiseMap,   NULL ) )) return false;
	if (FAILED( D3DX10CreateShaderResourceViewFromFile( g_pd3dDevice, (MediaFolder + "Burn.png").c_str() ,    NULL, NULL, &BurnMap,    NULL ) )) return false;
	if (FAILED( D3DX10CreateShaderResourceViewFromFile( g_pd3dDevice, (MediaFolder + "Distort.png").c_str() , NULL, NULL, &DistortMap, NULL ) )) return false;


	// Load and compile a separate effect file for post-processes.
	ID3D10Blob* pErrors;
	DWORD dwShaderFlags = D3D10_SHADER_ENABLE_STRICTNESS; // These "flags" are used to set the compiler options

	string fullFileName = ShaderFolder + "PostProcess.fx";
	if( FAILED( D3DX10CreateEffectFromFile( fullFileName.c_str(), NULL, NULL, "fx_4_0", dwShaderFlags, 0, g_pd3dDevice, NULL, NULL, &PPEffect, &pErrors, NULL ) ))
	{
		if (pErrors != 0)  MessageBox( NULL, reinterpret_cast<char*>(pErrors->GetBufferPointer()), "Error", MB_OK ); // Compiler error: display error message
		else               MessageBox( NULL, "Error loading FX file. Ensure your FX file is in the same folder as this executable.", "Error", MB_OK );  // No error message - probably file not found
		return false;
	}

	// There's an array of post-processing technique names above - get array of post-process techniques matching those names from the compiled effect file
	for (int pp = 0; pp < NumPostProcesses; pp++)
	{
		PPTechniques[pp] = PPEffect->GetTechniqueByName( PPTechniqueNames[pp].c_str() );
	}

	//assign Assignment techniques
	PPVariableTintTechnique = PPEffect->GetTechniqueByName("PPVariableTint");
	PPHighContrastTechnique = PPEffect->GetTechniqueByName("PPHighContrast");
	PPUnderwaterTechnique   = PPEffect->GetTechniqueByName("PPUnderwater"  );
	
	// Link to HLSL variables in post-process shaders
	SceneTextureVar      = PPEffect->GetVariableByName( "SceneTexture" )->AsShaderResource();
	PostProcessMapVar    = PPEffect->GetVariableByName( "PostProcessMap" )->AsShaderResource();
	PPAreaTopLeftVar     = PPEffect->GetVariableByName( "PPAreaTopLeft" )->AsVector();
	PPAreaBottomRightVar = PPEffect->GetVariableByName( "PPAreaBottomRight" )->AsVector();
	PPAreaDepthVar       = PPEffect->GetVariableByName( "PPAreaDepth" )->AsScalar();
	TintColourVar        = PPEffect->GetVariableByName( "TintColour" )->AsVector();
	VariableTintColourVar= PPEffect->GetVariableByName( "VariableTintColour" )->AsVector();
	NoiseScaleVar        = PPEffect->GetVariableByName( "NoiseScale" )->AsVector();
	NoiseOffsetVar       = PPEffect->GetVariableByName( "NoiseOffset" )->AsVector();
	DistortLevelVar      = PPEffect->GetVariableByName( "DistortLevel" )->AsScalar();
	BurnLevelVar         = PPEffect->GetVariableByName( "BurnLevel" )->AsScalar();
	SpiralTimerVar       = PPEffect->GetVariableByName( "SpiralTimer" )->AsScalar();
	HeatHazeTimerVar     = PPEffect->GetVariableByName( "HeatHazeTimer" )->AsScalar();

	return true;
}

void PostProcessShutdown()
{
	if (PPEffect)            PPEffect->Release();
    if (DistortMap)          DistortMap->Release();
    if (BurnMap)             BurnMap->Release();
    if (NoiseMap)            NoiseMap->Release();
	if (SceneShaderResource) SceneShaderResource->Release();
	if (SceneRenderTarget)   SceneRenderTarget->Release();
	if (SceneTexture)        SceneTexture->Release();
}

//*****************************************************************************


//-----------------------------------------------------------------------------
// Post Process Setup / Update
//-----------------------------------------------------------------------------

// Set up shaders for given post-processing filter (used for full screen and area processing)
void SelectPostProcess( PostProcesses filter )
{
	switch (filter)
	{
		case Tint:
		{
			D3DXCOLOR TintColour = D3DXCOLOR(1.0f,0.0f,0.0f,1.0f);
			TintColourVar->SetRawValue( &TintColour, 0, 12 );
		}
		break;

		case GreyNoise:
		{
			const float GrainSize = 140; // Fineness of the noise grain

			// Set shader constants - scale and offset for noise. Scaling adjusts how fine the noise is.
			CVector2 NoiseScale = CVector2( BackBufferWidth / GrainSize, BackBufferHeight / GrainSize );
			NoiseScaleVar->SetRawValue( &NoiseScale, 0, 8 );

			// The offset is randomised to give a constantly changing noise effect (like tv static)
			CVector2 RandomUVs = CVector2( Random( 0.0f,1.0f ),Random( 0.0f,1.0f ) );
			NoiseOffsetVar->SetRawValue( &RandomUVs, 0, 8 );

			// Set noise texture
			PostProcessMapVar->SetResource( NoiseMap );
		}
		break;

		case Burn:
		{
			// Set the burn level (value from 0 to 1 during animation)
			BurnLevelVar->SetFloat( BurnLevel );

			// Set burn texture
			PostProcessMapVar->SetResource( BurnMap );
		}
		break;

		case Distort:
		{
			// Set the level of distortion
			const float DistortLevel = 0.03f;
			DistortLevelVar->SetFloat( DistortLevel );

			// Set distort texture
			PostProcessMapVar->SetResource( DistortMap );
		}
		break;

		case Spiral:
		{
			// Set the amount of spiral - use a tweaked cos wave to animate
			SpiralTimerVar->SetFloat( (1.0f - Cos(SpiralTimer)) * 4.0f );
			break;
		}

		case HeatHaze:
		{
			// Set the amount of spiral - use a tweaked cos wave to animate
			HeatHazeTimerVar->SetFloat( HeatHazeTimer );
			break;
		}
	}

	if (VariableTint)
	{
		float r,g,b;
			const float saturation = 1.0f;
			const float value = 1.0f;

			int i = floor(TintHue*6);
			float f = TintHue * 6 - i;
			float p = value * (1 - saturation);
			float q = value * (1 - saturation * f);
			float t = value * (1 - saturation * (1 - f));
			
			switch(i)
			{
			case 0:
				r = value;
				g = t;
				b = p;
				break;
			case 1:
				r = q;
				g = value;
				b = p;
				break;
			case 2:
				r = p;
				g = value;
				b = t;
				break;
			case 3:
				r = p;
				g = q;
				b = value;
				break;
			case 4:
				r = t;
				g = p;
				b = value;
				break;
			case 5:
				r = value;
				g = p;
				b = q;
				break;
			}

			D3DXCOLOR TintColour = D3DXCOLOR(r,g,b,1.0f);
			VariableTintColourVar->SetRawValue( &TintColour, 0, 12 );
	}

	if (Underwater)
	{
		// Set the amount of spiral - use a tweaked cos wave to animate
		HeatHazeTimerVar->SetFloat( HeatHazeTimer );
	}
}

// Update post-processes (those that need updating) during scene update
void UpdatePostProcesses( float updateTime )
{
	// Not all post processes need updating
	BurnLevel = Mod( BurnLevel + BurnSpeed * updateTime, 1.0f );
	SpiralTimer   += SpiralSpeed * updateTime;
	HeatHazeTimer += HeatHazeSpeed * updateTime;

	//increment tintHue
	TintHue+=0.01f;
	if (TintHue > 1.0f) TintHue = 0.0f;

}




// Sets in the shaders the top-left, bottom-right and depth coordinates of the area post process to work on
// Requires a world point at the centre of the area, the width and height of the area (in world units), an optional depth offset (to pull or push 
// the effect of the post-processing into the scene). Also requires the camera, since we are creating a camera facing quad.
void SetPostProcessArea( CCamera* camera, CVector3 areaCentre, float width, float height, float depthOffset = 0.0f )
{
	// Get the area centre in camera space.
	CVector4 cameraSpaceCentre = CVector4(areaCentre, 1.0f) * camera->GetViewMatrix();

	// Get top-left and bottom-right of camera-facing area of required dimensions 
	cameraSpaceCentre.x -= width / 2;
	cameraSpaceCentre.y += height / 2; // Careful, y axis goes up here
	CVector4 cameraTopLeft = cameraSpaceCentre;
	cameraSpaceCentre.x += width;
	cameraSpaceCentre.y -= height;
	CVector4 cameraBottomRight = cameraSpaceCentre;

	// Get the projection space coordinates of the post process area
	CVector4 projTopLeft     = cameraTopLeft     * camera->GetProjMatrix();
	CVector4 projBottomRight = cameraBottomRight * camera->GetProjMatrix();

	// Perform perspective divide to get coordinates in normalised viewport space (-1.0 to 1.0 from left->right and bottom->top of the viewport)
	projTopLeft.x /= projTopLeft.w;
	projTopLeft.y /= projTopLeft.w;
	projBottomRight.x /= projBottomRight.w;
	projBottomRight.y /= projBottomRight.w;

	// Also do perspective divide on z to get depth buffer value for the area. Add the required depth offset (using an approximation)
	projTopLeft.z += depthOffset;
	projTopLeft.w += depthOffset;
	projTopLeft.z /= projTopLeft.w;

	// Convert the x & y coordinates to UV space (0 -> 1, y flipped). This extra step makes the shader work simpler
	projTopLeft.x =	 projTopLeft.x / 2.0f + 0.5f;
	projTopLeft.y = -projTopLeft.y / 2.0f + 0.5f;
	projBottomRight.x =	 projBottomRight.x / 2.0f + 0.5f;
	projBottomRight.y = -projBottomRight.y / 2.0f + 0.5f;

	// Send the values calculated to the shader. The post-processing vertex shader needs only these values to
	// create the vertex buffer for the quad to render, we don't need to create a vertex buffer for post-processing at all.
	PPAreaTopLeftVar->SetRawValue( &projTopLeft.Vector2(), 0, 8 );         // Viewport space x & y for top-left
	PPAreaBottomRightVar->SetRawValue( &projBottomRight.Vector2(), 0, 8 ); // Same for bottom-right
	PPAreaDepthVar->SetFloat( projTopLeft.z ); // Depth buffer value for area

	// ***NOTE*** Most applications you will see doing post-processing would continue here to create a vertex buffer in C++, and would
	// not use the unusual vertex shader that you will see in the .fx file here. That might (or might not) give a tiny performance boost,
	// but very tiny, if any (only a handful of vertices affected). I prefer this method because I find it cleaner and more flexible overall. 
}

// Set the top-left, bottom-right and depth coordinates for the area post process to work on for full-screen processing
// Since all post process code is now area-based, full-screen processing needs to explicitly set up the appropriate full-screen rectangle
void SetFullScreenPostProcessArea()
{
	CVector2 TopLeftUV     = CVector2( 0.0f, 0.0f ); // Top-left and bottom-right in UV space
	CVector2 BottomRightUV = CVector2( 1.0f, 1.0f );

	PPAreaTopLeftVar->SetRawValue( &TopLeftUV, 0, 8 );
	PPAreaBottomRightVar->SetRawValue( &BottomRightUV, 0, 8 );
	PPAreaDepthVar->SetFloat( 0.0f ); // Full screen depth set at 0 - in front of everything
}


//-----------------------------------------------------------------------------
// Game loop functions
//-----------------------------------------------------------------------------

// Draw one frame of the scene
void RenderScene()
{
	// Setup the viewport - defines which part of the back-buffer we will render to (usually all of it)
	D3D10_VIEWPORT vp;
	vp.Width  = BackBufferWidth;
	vp.Height = BackBufferHeight;
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	vp.TopLeftX = 0;
	vp.TopLeftY = 0;
	g_pd3dDevice->RSSetViewports( 1, &vp );


	//------------------------------------------------
	// SCENE RENDER PASS - rendering to a texture

	// Specify that we will render to the scene texture in this first pass (rather than the backbuffer), will share the depth/stencil buffer with the backbuffer though
	g_pd3dDevice->OMSetRenderTargets( 1, &SceneRenderTarget, DepthStencilView );

	// Clear the texture and the depth buffer
	g_pd3dDevice->ClearRenderTargetView( SceneRenderTarget, &AmbientColour.r );
	g_pd3dDevice->ClearDepthStencilView( DepthStencilView, D3D10_CLEAR_DEPTH, 1.0f, 0 );

	// Prepare camera
	MainCamera->SetAspect( static_cast<TFloat32>(BackBufferWidth) / BackBufferHeight );
	MainCamera->CalculateMatrices();
	MainCamera->CalculateFrustrumPlanes();

	// Set camera and light data in shaders
	SetCamera( MainCamera );
	SetAmbientLight( AmbientColour );
	SetLights( &Lights[0] );

	// Render entities
	EntityManager.RenderAllEntities( MainCamera );

	//------------------------------------------------


	//------------------------------------------------
	// FULL SCREEN POST PROCESS RENDER PASS - Render full screen quad on the back-buffer mapped with the scene texture, with post-processing

	// Select the back buffer to use for rendering (will ignore depth-buffer for full-screen quad) and select scene texture for use in shader
	g_pd3dDevice->OMSetRenderTargets( 1, &BackBufferRenderTarget, DepthStencilView ); // No need to clear the back-buffer, we're going to overwrite it all
	SceneTextureVar->SetResource( SceneShaderResource );

	// Prepare shader settings for the current full screen filter
	SelectPostProcess( FullScreenFilter );
	SetFullScreenPostProcessArea(); // Define the full-screen as the area to affect

	// Using special vertex shader than creates its own data for a full screen quad (see .fx file). No need to set vertex/index buffer, just draw 4 vertices of quad
	// Select technique to match currently selected post-process
	g_pd3dDevice->IASetInputLayout( NULL );
	g_pd3dDevice->IASetPrimitiveTopology( D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP );
	PPTechniques[FullScreenFilter]->GetPassByIndex(0)->Apply(0);
	g_pd3dDevice->Draw( 4, 0 );

	//------------------------------------------------
	//add additional passes for any stacking post-process techniques
	if (HighContrast)
	{
		PPHighContrastTechnique->GetPassByIndex(0)->Apply(0);
		g_pd3dDevice->Draw( 4, 0 );
	}

	if (VariableTint)
	{
		PPVariableTintTechnique->GetPassByIndex(0)->Apply(0);
		g_pd3dDevice->Draw( 4, 0 );
	}

	
	if (Underwater)
	{
		PPUnderwaterTechnique->GetPassByIndex(0)->Apply(0);
		g_pd3dDevice->Draw( 4, 0 );
	}

	//**|PPPOLY|***************************************
	// POLY POST PROCESS RENDER PASS
	// The scene has been rendered in full into a texture then copied to the back-buffer. However, the post-processed polygons were missed out. Now render the entities
	// again, but only the post-processed materials. These are rendered to the back-buffer in the correct places in the scene, but most importantly their shaders will
	// have the scene texture available to them. So these polygons can distort or affect the scene behind them (e.g. distortion through cut glass). Note that this also
	// means we can do blending (additive, multiplicative etc.) in the shader. The post-processed materials are identified with a boolean (RenderMethod.cpp). This entire
	// approach works even better with "bucket" rendering, where post-process shaders are held in a separate bucket - making it unnecessary to "RenderAllEntities" as 
	// we are doing here.

	// NOTE: Post-processing - need to set the back buffer as a render target. Relying on the fact that the section above already did that
	// Polygon post-processing occurs in the scene rendering code (RenderMethod.cpp) - so pass over the scene texture and viewport dimensions for the scene post-processing materials/shaders
	SetSceneTexture( SceneShaderResource, BackBufferWidth, BackBufferHeight );

	// Render all entities again, but flag that we only want the post-processed polygons
	EntityManager.RenderAllEntities( MainCamera, true );

	//************************************************


	//------------------------------------------------
	// AREA POST PROCESS RENDER PASS - Render smaller quad on the back-buffer mapped with a matching area of the scene texture, with different post-processing

	// NOTE: Post-processing - need to render to the back buffer and select scene texture for use in shader. Relying on the fact that the section above already did that

	// Will have post-processed area over the moving cube
	CEntity* cubey = EntityManager.GetEntity( "Cubey" );

	// Set the area size, 20 units wide and high, 0 depth offset. This sets up a viewport space quad for the post-process to work on
	// Note that the function needs the camera to turn the cube's point into a camera facing rectangular area
	SetPostProcessArea( MainCamera, cubey->Position(), 20, 20, -9 );

	// Select one of the post-processing techniques and render the area using it
	SelectPostProcess( Spiral ); // Make sure you also update the line below when you change the post-process method here!
	g_pd3dDevice->IASetInputLayout( NULL );
	g_pd3dDevice->IASetPrimitiveTopology( D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP );
	PPTechniques[Spiral]->GetPassByIndex(0)->Apply(0);
	g_pd3dDevice->Draw( 4, 0 );

	//------------------------------------------------

	//Repeat for the hole in the wall
	CEntity* Wall = EntityManager.GetEntity( WallUID );

	// Set the area size, 20 units wide and high, 0 depth offset. This sets up a viewport space quad for the post-process to work on
	// Note that the function needs the camera to turn the cube's point into a camera facing rectangular area

	//Alter the position with an offset
	CVector3 WallOffset(0,25,0);
	WallOffset = WallOffset + Wall->Position();
	SetPostProcessArea( MainCamera, WallOffset, 30, 30, -9 );

	// Select one of the post-processing techniques and render the area using it
	SelectPostProcess( Spiral ); // Make sure you also update the line below when you change the post-process method here!
	g_pd3dDevice->IASetInputLayout( NULL );
	g_pd3dDevice->IASetPrimitiveTopology( D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP );
	PPTechniques[Spiral]->GetPassByIndex(0)->Apply(0);
	g_pd3dDevice->Draw( 4, 0 );


	// These two lines unbind the scene texture from the shader to stop DirectX issuing a warning when we try to render to it again next frame
	SceneTextureVar->SetResource( 0 );
	PPTechniques[FullScreenFilter]->GetPassByIndex(0)->Apply(0);

	// Render UI elements last - don't want them post-processed
	RenderSceneText();

	// Present the backbuffer contents to the display
	SwapChain->Present( 0, 0 );
}


// Render a single text string at the given position in the given colour, may optionally centre it
void RenderText( const string& text, int X, int Y, float r, float g, float b, bool centre = false )
{
	RECT rect;
	if (!centre)
	{
		SetRect( &rect, X, Y, 0, 0 );
		OSDFont->DrawText( NULL, text.c_str(), -1, &rect, DT_NOCLIP, D3DXCOLOR( r, g, b, 1.0f ) );
	}
	else
	{
		SetRect( &rect, X - 100, Y, X + 100, 0 );
		OSDFont->DrawText( NULL, text.c_str(), -1, &rect, DT_CENTER | DT_NOCLIP, D3DXCOLOR( r, g, b, 1.0f ) );
	}
}

// Render on-screen text each frame
void RenderSceneText()
{
	// Write FPS text string
	stringstream outText;
	if (AverageUpdateTime >= 0.0f)
	{
		outText << "Frame Time: " << AverageUpdateTime * 1000.0f << "ms" << endl << "FPS:" << 1.0f / AverageUpdateTime;
		RenderText( outText.str(), 2, 2, 0.0f, 0.0f, 0.0f );
		RenderText( outText.str(), 0, 0, 1.0f, 1.0f, 0.0f );
		outText.str("");
	}

	// Output post-process name
	outText << "Fullscreen Post-Process: ";
	switch (FullScreenFilter)
	{
	case Copy: 
		outText << "Copy";
		break;
	case Tint: 
		outText << "Tint";
		break;
	case GreyNoise: 
		outText << "Grey Noise";
		break;
	case Burn: 
		outText << "Burn";
		break;
	case Distort: 
		outText << "Distort";
		break;
	case Spiral: 
		outText << "Spiral";
		break;
	case HeatHaze: 
		outText << "Heat Haze";
		break;
	}

	//Output assignment post prosesses
	outText << endl << "- Active Assignemtn PostProcesses-" << endl;
	
	if (VariableTint) outText << "Variable Tint " << endl;
	if (HighContrast) outText << "High Contrast " << endl;
	if (Underwater  ) outText << "Underwater"     << endl;
	RenderText( outText.str(),  0, 32,  1.0f, 1.0f, 1.0f );
}


// Update the scene between rendering
void UpdateScene( float updateTime )
{
	// Call all entity update functions
	EntityManager.UpdateAllEntities( updateTime );

	// Update any post processes that need updates
	UpdatePostProcesses( updateTime );

	// Set camera speeds
	// Key F1 used for full screen toggle
	if (KeyHit( Key_F2 )) CameraMoveSpeed = 5.0f;
	if (KeyHit( Key_F3 )) CameraMoveSpeed = 40.0f;
	if (KeyHit( Key_F4 )) CameraMoveSpeed = 160.0f;
	if (KeyHit( Key_F5 )) CameraMoveSpeed = 640.0f;

	// Choose post-process
	if (KeyHit( Key_Numpad1 )) FullScreenFilter = Copy;
	if (KeyHit( Key_Numpad2 )) FullScreenFilter = Tint;
	if (KeyHit( Key_Numpad3 )) FullScreenFilter = GreyNoise;
	if (KeyHit( Key_Numpad4 )) FullScreenFilter = Burn;
	if (KeyHit( Key_Numpad5 )) FullScreenFilter = Distort;
	if (KeyHit( Key_Numpad6 )) FullScreenFilter = Spiral;
	if (KeyHit( Key_Numpad7 )) FullScreenFilter = HeatHaze;

	//Enable assignment Post-processes
	if (KeyHit( Key_1 )) VariableTint = !VariableTint;
	if (KeyHit( Key_2 )) HighContrast = !HighContrast;
	if (KeyHit( Key_3 )) Underwater = !Underwater;

	// Rotate cube and attach light to it
	CEntity* cubey = EntityManager.GetEntity( "Cubey" );
	cubey->Matrix().RotateX( ToRadians(53.0f) * updateTime );
	cubey->Matrix().RotateZ( ToRadians(42.0f) * updateTime );
	cubey->Matrix().RotateWorldY( ToRadians(12.0f) * updateTime );
	Lights[1]->SetPosition( cubey->Position() );
	
	// Rotate polygon post-processed entity
	CEntity* ppEntity = EntityManager.GetEntity( "PostProcessBlock" );
	ppEntity->Matrix().RotateY( ToRadians(30.0f) * updateTime );

	// Move the camera
	MainCamera->Control( Key_Up, Key_Down, Key_Left, Key_Right, Key_W, Key_S, Key_A, Key_D, 
	                     CameraMoveSpeed * updateTime, CameraRotSpeed * updateTime );

	// Accumulate update times to calculate the average over a given period
	SumUpdateTimes += updateTime;
	++NumUpdateTimes;
	if (SumUpdateTimes >= UpdateTimePeriod)
	{
		AverageUpdateTime = SumUpdateTimes / NumUpdateTimes;
		SumUpdateTimes = 0.0f;
		NumUpdateTimes = 0;
	}
}


} // namespace gen
