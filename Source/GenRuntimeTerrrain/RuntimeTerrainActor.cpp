// Fill out your copyright notice in the Description page of Project Settings.


#include "RuntimeTerrainActor.h"
#include "Async/Async.h"
#include "ImageUtils.h"                 
#include "IImageWrapper.h"            
#include "IImageWrapperModule.h"       
#include "Modules/ModuleManager.h"   
#include "Misc/FileHelper.h"          
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "HAL/PlatformFilemanager.h"

ARuntimeTerrainActor::ARuntimeTerrainActor()
{
	PrimaryActorTick.bCanEverTick = true;

	PrimaryActorTick.bCanEverTick = false;

	ProceduralMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ProceduralMesh"));
	SetRootComponent(ProceduralMesh);
}

void ARuntimeTerrainActor::BeginPlay()
{
	Super::BeginPlay();
}

void ARuntimeTerrainActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}


void ARuntimeTerrainActor::GenerateTerrainFromTexture(float HeightScale)
{
	if (!HeightmapTexture) return;

	// Lock heightmap mip data
	FTexture2DMipMap& Mip = HeightmapTexture->GetPlatformData()->Mips[0];
	int32 Width = Mip.SizeX;
	int32 Height = Mip.SizeY;

	void* DataPtr = Mip.BulkData.Lock(LOCK_READ_ONLY);
	FColor* FormattedImageData = static_cast<FColor*>(DataPtr);

	// Terrain grid settings
	const float GridSpacing = 100.0f;

	TArray<FVector> Vertices;
	TArray<FVector2D> UVs;
	TArray<int32> Triangles;

	// Generate vertices and UVs
	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			int32 Index = Y * Width + X;
			FColor Color = FormattedImageData[Index];

			// Use luminance to calculate height
			float Grayscale = (Color.R * 0.299f + Color.G * 0.587f + Color.B * 0.114f) / 255.0f;
			float Z = Grayscale * HeightScale;

			Vertices.Add(FVector(X * GridSpacing, Y * GridSpacing, Z));
			UVs.Add(FVector2D((float)X / (Width - 1), (float)Y / (Height - 1)));
		}
	}

	// Generate triangles
	for (int32 Y = 0; Y < Height - 1; ++Y)
	{
		for (int32 X = 0; X < Width - 1; ++X)
		{
			int32 I0 = Y * Width + X;
			int32 I1 = I0 + 1;
			int32 I2 = I0 + Width;
			int32 I3 = I2 + 1;

			// Triangle 1: I0, I2, I1
			// Triangle 2: I1, I2, I3
			Triangles.Append({ I0, I2, I1 });
			Triangles.Append({ I1, I2, I3 });
		}
	}

	Mip.BulkData.Unlock();

	// Optional: Calculate flat normals (not done here, Unreal handles basic lighting fallback)
	TArray<FVector> Normals;
	const TArray<FLinearColor> VertexColors;
	TArray<FProcMeshTangent> Tangents;

	ProceduralMesh->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, VertexColors, Tangents, true);

	// Apply surface texture to dynamic material
	if (BaseMaterial && SurfaceTexture)
	{
		UMaterialInstanceDynamic* DynMat = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		DynMat->SetTextureParameterValue("BaseTexture", SurfaceTexture);
		ProceduralMesh->SetMaterial(0, DynMat);
	}
}



UTexture2D* ARuntimeTerrainActor::LoadTextureFromDisk(const FString& ImagePath)
{
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));

	TArray<uint8> RawFileData;
	if (!FFileHelper::LoadFileToArray(RawFileData, *ImagePath))
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to load file %s"), *ImagePath);
		return nullptr;
	}

	EImageFormat Format = ImageWrapperModule.DetectImageFormat(RawFileData.GetData(), RawFileData.Num());
	if (Format == EImageFormat::Invalid)
	{
		UE_LOG(LogTemp, Error, TEXT("Unsupported format: %s"), *ImagePath);
		return nullptr;
	}

	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(Format);

	if (ImageWrapper->SetCompressed(RawFileData.GetData(), RawFileData.Num()))
	{
		TArray64<uint8> UncompressedRGBA;
		if (ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, UncompressedRGBA))
		{
			int32 Width = ImageWrapper->GetWidth();
			int32 Height = ImageWrapper->GetHeight();

			UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
			if (!Texture) return nullptr;

			void* TextureData = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
			FMemory::Memcpy(TextureData, UncompressedRGBA.GetData(), UncompressedRGBA.Num());
			Texture->GetPlatformData()->Mips[0].BulkData.Unlock();

			Texture->UpdateResource();
			return Texture;
		}
	}

	return nullptr;
}




void ARuntimeTerrainActor::GenerateTerrainAsync(float HeightScale)
{
	if (!HeightmapTexture) return;

	UTexture2D* LocalTexture = HeightmapTexture;

	// Copy this, texture and scale explicitly (C++20 safe)
	Async(EAsyncExecution::ThreadPool, [this, LocalTexture, HeightScale]()
	{
		bCancelGeneration = false;
		bIsGeneratingTerrain = true;
		GenerationProgress = 0.0f;

		FTexture2DMipMap& Mip = LocalTexture->GetPlatformData()->Mips[0];
		int32 Width = Mip.SizeX;
		int32 Height = Mip.SizeY;

		void* DataPtr = Mip.BulkData.Lock(LOCK_READ_ONLY);
		FColor* FormattedImageData = static_cast<FColor*>(DataPtr);

		const float GridSpacing = 100.0f;

		TArray<FVector> Vertices;
		TArray<FVector2D> UVs;
		TArray<int32> Triangles;

		int32 Total = Width * Height;
		int32 Step = FMath::Max(1, Total / 100); // update every ~1%

		for (int32 Y = 0; Y < Height && !bCancelGeneration; ++Y)
		{
			for (int32 X = 0; X < Width && !bCancelGeneration; ++X)
			{
				int32 Index = Y * Width + X;
				FColor Color = FormattedImageData[Index];
				float Grayscale = (Color.R * 0.299f + Color.G * 0.587f + Color.B * 0.114f) / 255.0f;
				float Z = Grayscale * HeightScale;
				

				Vertices.Add(FVector(X * GridSpacing, Y * GridSpacing, Z));
				UVs.Add(FVector2D((float)X / (Width - 1), (float)Y / (Height - 1)));

				if ((Index % Step) == 0)
				{
					GenerationProgress = (float)Index / Total;
				}
			}
		}

		Mip.BulkData.Unlock();

		if (bCancelGeneration)
		{
			bIsGeneratingTerrain = false;
			UE_LOG(LogTemp, Warning, TEXT("Terrain generation canceled."));
			return;
		}

		// Triangle generation
		for (int32 Y = 0; Y < Height - 1; ++Y)
		{
			for (int32 X = 0; X < Width - 1; ++X)
			{
				int32 I0 = Y * Width + X;
				int32 I1 = I0 + 1;
				int32 I2 = I0 + Width;
				int32 I3 = I2 + 1;

				Triangles.Append({ I0, I2, I1 });
				Triangles.Append({ I1, I2, I3 });
			}
		}

		// Push mesh creation to game thread
		AsyncTask(ENamedThreads::GameThread, [this, Vertices = MoveTemp(Vertices), Triangles = MoveTemp(Triangles), UVs = MoveTemp(UVs)]()
		{
			bIsGeneratingTerrain = false;
			GenerationProgress = 1.0f;

			TArray<FVector> Normals;
			const TArray<FLinearColor> VertexColors;
			TArray<FProcMeshTangent> Tangents;

			ProceduralMesh->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, VertexColors, Tangents, true);

			if (BaseMaterial && SurfaceTexture)
			{
				UMaterialInstanceDynamic* DynMat = UMaterialInstanceDynamic::Create(BaseMaterial, this);
				DynMat->SetTextureParameterValue("BaseTexture", SurfaceTexture);
				ProceduralMesh->SetMaterial(0, DynMat);
			}

			OnTerrainGenerationComplete.Broadcast();
		});
	});
}



void ARuntimeTerrainActor::CancelChunkedGeneration()
{
	if (!bIsGeneratingTerrain)
		return;

	// Clear timers
	GetWorld()->GetTimerManager().ClearTimer(TerrainGenerationTimerHandle);
	GetWorld()->GetTimerManager().ClearTimer(TriangleGenerationTimerHandle);

	// Unlock texture if needed
	if (CachedHeightmapData)
	{
		HeightmapTexture->GetPlatformData()->Mips[0].BulkData.Unlock();
		CachedHeightmapData = nullptr;
	}

	// Reset state
	BatchedVertices.Reset();
	BatchedUVs.Reset();
	BatchedTriangles.Reset();
	bIsGeneratingTerrain = false;
	GenerationProgress = 0.0f;

	GenerationStatus = TEXT("Cancelled");
	UE_LOG(LogTemp, Warning, TEXT("Terrain generation cancelled."));
}



void ARuntimeTerrainActor::StartChunkedTerrainGeneration(float HeightScale)
{

	if (bIsGeneratingTerrain)
	{
		GenerationStatus = TEXT("Terrain is already generating.");
		UE_LOG(LogTemp, Warning, TEXT("Terrain is already generating."));
		return;
	}
	
	if (!HeightmapTexture) return;

	FTexture2DMipMap& Mip = HeightmapTexture->GetPlatformData()->Mips[0];
	HeightmapWidth = Mip.SizeX;
	HeightmapHeight = Mip.SizeY;
	
	UE_LOG(LogTemp, Log, TEXT("Heightmap Size: %d x %d"), HeightmapWidth, HeightmapHeight);
	
	// Optional: Safety cap for massive maps
	if (HeightmapWidth > 4097 || HeightmapHeight > 4097)
	{
		GenerationStatus = TEXT("Heightmap is too large.");
		UE_LOG(LogTemp, Error, TEXT("Heightmap is too large."));
		return;
	}
	
	GenerationStatus = TEXT("Generating Terrain...");
	
	CachedHeightScale = HeightScale;

	void* DataPtr = Mip.BulkData.Lock(LOCK_READ_ONLY);
	CachedHeightmapData = static_cast<FColor*>(DataPtr);

	BatchedVertices.Reset();
	BatchedUVs.Reset();
	BatchedTriangles.Reset();
	
	CurrentY = 0;
	CurrentTriangleY = 0;
	GenerationProgress = 0.0f;
	bIsGeneratingTerrain = true;

	GetWorld()->GetTimerManager().ClearTimer(TerrainGenerationTimerHandle);
	GetWorld()->GetTimerManager().SetTimer(
		TerrainGenerationTimerHandle,
		this,
		&ARuntimeTerrainActor::ProcessTerrainChunk,
		0.01f,
		true
	);
}

void ARuntimeTerrainActor::ProcessTerrainChunk()
{

	GenerationStatus = TEXT("Generating Triangles...");

	
	const int32 RowsPerTick = 32;
	const float GridSpacing = 100.0f;

	for (int32 Row = 0; Row < RowsPerTick && CurrentY < HeightmapHeight; ++Row, ++CurrentY)
	{
		for (int32 X = 0; X < HeightmapWidth; ++X)
		{
			int32 Index = CurrentY * HeightmapWidth + X;
			FColor Color = CachedHeightmapData[Index];
			float Grayscale = (Color.R * 0.299f + Color.G * 0.587f + Color.B * 0.114f) / 255.0f;
			float Z = Grayscale * CachedHeightScale;

			BatchedVertices.Add(FVector(X * GridSpacing, CurrentY * GridSpacing, Z));
			BatchedUVs.Add(FVector2D((float)X / (HeightmapWidth - 1), (float)CurrentY / (HeightmapHeight - 1)));
		}
	}

	// GenerationProgress = (float)CurrentY / HeightmapHeight;
	GenerationProgress = (HeightmapHeight > 0) ? (float)CurrentY / HeightmapHeight : 1.0f;
	
	if (CurrentY >= HeightmapHeight)
	{
		if (CachedHeightmapData)
		{
			HeightmapTexture->GetPlatformData()->Mips[0].BulkData.Unlock();
			CachedHeightmapData = nullptr;
		}

		GetWorld()->GetTimerManager().ClearTimer(TerrainGenerationTimerHandle);

		// Start triangle chunk processing
		GetWorld()->GetTimerManager().SetTimer(
			TriangleGenerationTimerHandle,
			this,
			&ARuntimeTerrainActor::ProcessTriangleChunk,
			0.01f,
			true
		);
	}
}


void ARuntimeTerrainActor::ProcessTriangleChunk()
{
	const int32 RowsPerTick = 32;

	for (int32 Row = 0; Row < RowsPerTick && CurrentTriangleY < HeightmapHeight - 1; ++Row, ++CurrentTriangleY)
	{
		for (int32 X = 0; X < HeightmapWidth - 1; ++X)
		{
			int32 I0 = CurrentTriangleY * HeightmapWidth + X;
			int32 I1 = I0 + 1;
			int32 I2 = I0 + HeightmapWidth;
			int32 I3 = I2 + 1;

			BatchedTriangles.Append({ I0, I2, I1 });
			BatchedTriangles.Append({ I1, I2, I3 });
		}
	}

	// Optional: progress granularity
	GenerationProgress = 0.9f + 0.1f * (float)CurrentTriangleY / (HeightmapHeight - 1);
	
	if (CurrentTriangleY >= HeightmapHeight - 1)
	{
		GetWorld()->GetTimerManager().ClearTimer(TriangleGenerationTimerHandle);
		FinalizeTerrainMesh();
	}
}


void ARuntimeTerrainActor::FinalizeTerrainMesh()
{
	TArray<FVector> Normals;
	const TArray<FLinearColor> VertexColors;
	TArray<FProcMeshTangent> Tangents;

	ProceduralMesh->CreateMeshSection_LinearColor(0, BatchedVertices, BatchedTriangles, Normals, BatchedUVs, VertexColors, Tangents, true);

	if (BaseMaterial && SurfaceTexture)
	{
		UMaterialInstanceDynamic* DynMat = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		DynMat->SetTextureParameterValue("BaseTexture", SurfaceTexture);
		ProceduralMesh->SetMaterial(0, DynMat);
	}

	GenerationProgress = 1.0f;
	bIsGeneratingTerrain = false;
	GenerationStatus = TEXT("Completed");
	OnTerrainGenerationComplete.Broadcast();
}
