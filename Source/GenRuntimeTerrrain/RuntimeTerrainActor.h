// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "RuntimeTerrainActor.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTerrainGenerationComplete);

UCLASS()
class GENRUNTIMETERRRAIN_API ARuntimeTerrainActor : public AActor
{
	GENERATED_BODY()

public:
	ARuntimeTerrainActor();

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;

	
	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	float GenerationProgress = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	FString GenerationStatus = TEXT("Idle");
	
	// Cancellation flag
	std::atomic<bool> bCancelGeneration = false;
	
	UPROPERTY(BlueprintReadOnly, Category = "Terrain")
	bool bIsGeneratingTerrain = false;


	UFUNCTION(BlueprintCallable, Category = "Terrain")
	void CancelChunkedGeneration();

	UFUNCTION(BlueprintCallable, Category = "Terrain")
	float GetGenerationProgress() const { return GenerationProgress; }

	UFUNCTION(BlueprintCallable, Category = "Terrain")
	FString GetGenerationStatus() const { return GenerationStatus; }
	
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	UProceduralMeshComponent* ProceduralMesh;

	/** Heightmap to generate terrain from (grayscale) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Terrain")
	UTexture2D* HeightmapTexture;

	/** Texture to apply to the mesh surface */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Terrain")
	UTexture2D* SurfaceTexture;

	/** Material that supports a texture parameter called "BaseTexture" */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Terrain")
	UMaterialInterface* BaseMaterial;


	UFUNCTION(BlueprintCallable, Category="Terrain")
	void GenerateTerrainFromTexture(float HeightScale = 100.0f);
	
	UFUNCTION(BlueprintCallable, Category = "Terrain|Utility")
	UTexture2D* LoadTextureFromDisk(const FString& ImagePath);
	
	UFUNCTION(BlueprintCallable, Category = "Terrain|Utility")
	void GenerateTerrainAsync(float HeightScale);
	

	UPROPERTY(BlueprintAssignable, Category = "Terrain")
	FOnTerrainGenerationComplete OnTerrainGenerationComplete;

	UFUNCTION(BlueprintCallable, Category = "Terrain|Utility")
	void StartChunkedTerrainGeneration(float HeightScale);

	void ProcessTerrainChunk();
	void ProcessTriangleChunk();
	void FinalizeTerrainMesh();
	

	// For batched generation
	FTimerHandle TerrainGenerationTimerHandle;
	int32 CurrentY = 0;
	TArray<FVector> BatchedVertices;
	TArray<FVector2D> BatchedUVs;
	TArray<int32> BatchedTriangles;
	int32 HeightmapWidth = 0;
	int32 HeightmapHeight = 0;
	FColor* CachedHeightmapData = nullptr;
	float CachedHeightScale = 100.0f;

	FTimerHandle TriangleGenerationTimerHandle;
	int32 CurrentTriangleY = 0;

};
