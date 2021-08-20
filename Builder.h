// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "Builder.generated.h"

USTRUCT(BlueprintType)
struct FBuildingInfo
{
GENERATED_BODY();
	int32 code;
	double height;
	TArray<FVector> coords;
};

USTRUCT(BlueprintType)
struct FGeoBuildingLayerInfo
{
GENERATED_BODY()
	int32 layer_id;
	FString url;
	float opacity;
	
	float top_roughness;
	float top_metalness;
	TMap<float, FString> top_condition;

	float side_roughness;
	float side_metalness;
	TMap<float, FString> side_condition;
};

UENUM(BlueprintType)
enum class FRaySegmentCrossType :uint8
{
	Cross_None = 0,       //���ཻ
	Cross_WithStart = 1,  //�ཻ���߶����
	Cross_WithEnd = 2,    //������߶��յ�
	Cross_Nomal = 3       //�������߶ν���    
};

UCLASS()
class BUILDINGBUILDER_API ABuilder : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ABuilder();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;


	UFUNCTION(BlueprintCallable, Category = "Builder")
		void SetPath(const FString& path);

	UFUNCTION(BlueprintCallable, Category = "Builder")
		bool ParseJson();
		
	UFUNCTION(BlueprintCallable, Category = "Builder")
		void CreateMesh();




protected:
	bool ParseMapJson();
	bool ParseBuildingsJson();
	bool getJsonRootObjectFromFile(FString file_name, TSharedPtr<FJsonObject>& json_roo);
	
	FVector Lonlat2Mercator(double lon,double lat, double height = 0.0);
	void ProcessCoords(double ref_x = 0.0,double ref_y = 0.0);

	void CreateSideMesh();
	void CreateSideMesh_PMCImp();
	void CreateSideMesh_RawMeshImp();

	void CreateTopMesh();
	void CreateTopMesh_PMCImp();
	void CreateTopMesh_RawMeshImp();

	void divideConvexPolygon_PMCImp(TArray<FVector> polygon, double height, TArray<FVector>& Vertex, TArray<int32>& Index, TArray<FVector2D>& UV);
	void divideConvexPolygon_RawMeshImp(TArray<FVector> polygon, double height, FRawMesh& RawMesh);
	void divideConcavePolygon_PMCImp(TArray<FVector> polygon, double height, TArray<FVector>& Vertex, TArray<int32>& Index, TArray<FVector2D>& UV);
	void divideConcavePolygon_RawMeshImp(TArray<FVector> polygon, double height, FRawMesh& RawMesh);

	bool LoadImageToTexture2D(const FString& ImagePath, UTexture2D*& InTexture, float& Width, float& Height);
	UMaterialInterface* CreateMaterialInstanceDynamic(UTexture2D* InTexture,float Roughness,float Metallic );
	UMaterialInterface* CreateMaterial(UTexture2D*& InTexture, FString material_name, float Roughness, float Metallic);

	//���Ƿ����ߵ��Ҳ�
	bool pointRightOfLine(FVector pStart, FVector pEnd, FVector point);
	//���Ƿ�����������--�����ζ���Ϊ˳ʱ��
	bool pointInTriangle(TArray<FVector> triangle, FVector point);	
	//�߶��Ƿ���ĳ�������Y�����������ཻ
	FRaySegmentCrossType segmentCrossWithYFowardRayWithoutZ(FVector pStart, FVector pEnd, FVector point);
	//�߶����߶��Ƿ��ཻ
	bool isSegmentCrossWithoutZ(FVector pStart1, FVector pEnd1, FVector pStart2, FVector pEnd2);
	// ���Ƿ��ڶ������
	bool pointInPolygon(TArray<FVector> polygon, FVector point);
	//�Ƿ�Ϊ͹����
	bool isConvexPoint(TArray<FVector> polygon, int32 index);
	//�Ƿ�Ϊ͹�����
	bool isConvexPolygon(TArray<FVector> polygon);
	//�Ƿ�Ϊ�ɷָ��
	bool isDivisiblePoint(TArray<FVector> polygon, int32 index);
	//�Ƿ�Ϊ����ĵ㣨���ߵ㣩
	bool isSurplusPoint(TArray<FVector> polygon, int32 index);
protected:
	UPROPERTY(EditAnywhere)
		UProceduralMeshComponent* side_pmc;
	UPROPERTY(EditAnywhere)
		UProceduralMeshComponent* top_pmc;

private:
	FString m_file_path;
	TMap<int32, FGeoBuildingLayerInfo> m_building_layer_info;
	TMap<int32, TArray<FBuildingInfo>> m_building_layer_data;
	bool m_use_pmc;
};


