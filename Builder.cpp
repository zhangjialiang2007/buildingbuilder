// Fill out your copyright notice in the Description page of Project Settings.
#include "Builder.h"
#include "Serialization/JsonSerializer.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Factories/MaterialFactoryNew.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/FileHelper.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RawMesh.h"
#include "Engine/Texture2D.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"




const float	threshold = FLT_EPSILON;
// Sets default values
ABuilder::ABuilder()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	m_file_path = FPaths::ProjectDir() + "Data/";
	m_use_pmc = false;
	wall_pmc = CreateDefaultSubobject<UProceduralMeshComponent>("wall_pmc");
	wall_pmc->SetupAttachment(GetRootComponent());
	m_wall_top_dis = 1.0;
	m_wall_bottom_dis = 2.0;

	roof_pmc = CreateDefaultSubobject<UProceduralMeshComponent>("roof_pmc");
	roof_pmc->SetupAttachment(GetRootComponent());
	
	


	//Material = nullptr;
}

// Called when the game starts or when spawned
void ABuilder::BeginPlay()
{
	Super::BeginPlay();

}

// Called every frame
void ABuilder::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

void ABuilder::SetPath(const FString& path)
{
	m_file_path = path;
	if (!m_file_path.EndsWith(TEXT("/")))
	{
		m_file_path += TEXT("/");
	}
}
bool ABuilder::ParseJson()
{
	if (!ParseMapJson())
	{
		return false;
	}

	if (!ParseBuildingsJson())
	{
		return false;
	}

	return true;
}
bool ABuilder::ParseMapJson()
{
	FString path = m_file_path + "map.json";
	if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*path))
	{
		FString errorMsg = path + "文件不存在.";
		UE_LOG(LogClass, Error, TEXT("===%s==="), *errorMsg);
		return false;
	}
	FString map_json = "";
	if (!FFileHelper::LoadFileToString(map_json, *(path)) || map_json == "")
	{
		FString errorMsg = path + "加载文件失败.";
		UE_LOG(LogClass, Error, TEXT("===%s==="), *errorMsg);
		return false;
	}

	m_building_layer_info.Empty();
	TSharedRef< TJsonReader<> > Reader = TJsonReaderFactory<>::Create(map_json);
	TSharedPtr<FJsonObject> rRoot;
	if (FJsonSerializer::Deserialize(Reader, rRoot))
	{
		if (rRoot->HasField(TEXT("data")))
		{
			TSharedPtr<FJsonObject> data = rRoot->GetObjectField(TEXT("data"));
			TArray<TSharedPtr<FJsonValue>> layers = data->GetArrayField(TEXT("layers"));
			for (int i = 0; i < layers.Num(); i++)
			{
				const TSharedPtr<FJsonObject>* layer;
				if (layers[i].Get()->TryGetObject(layer))
				{
					FString geometry_type = layer->Get()->GetStringField(TEXT("geometryType"));
					if (geometry_type == "GeoBuilding")
					{
						FGeoBuildingLayerInfo building_layer_info;
						building_layer_info.layer_id = layer->Get()->GetIntegerField(TEXT("id"));
						building_layer_info.url = layer->Get()->GetStringField(TEXT("url"));

						TSharedPtr<FJsonObject> layerConfig = layer->Get()->GetObjectField(TEXT("layerConfig"));
						if (layerConfig == nullptr)
						{
							UE_LOG(LogClass, Error, TEXT("layerConfig is null, id = %d"), building_layer_info.layer_id);
							continue;
						}
						building_layer_info.opacity = layerConfig->GetNumberField(TEXT("opacity"));
						TArray<TSharedPtr<FJsonValue>> roughness = layerConfig->GetArrayField(TEXT("roughness"));
						building_layer_info.roof_roughness = roughness[0].Get()->AsNumber();
						building_layer_info.wall_roughness = roughness[1].Get()->AsNumber();

						TArray<TSharedPtr<FJsonValue>> metalness = layerConfig->GetArrayField(TEXT("metalness"));
						building_layer_info.roof_metalness = metalness[0].Get()->AsNumber();
						building_layer_info.wall_metalness = metalness[1].Get()->AsNumber();


						TArray<TSharedPtr<FJsonValue>> imageUrls = layerConfig->GetArrayField(TEXT("imageUrl"));
						for (int j = 0; j < imageUrls.Num(); j++)
						{
							const TSharedPtr<FJsonObject>* imageUrl;
							if (imageUrls[j].Get()->TryGetObject(imageUrl))
							{
								FString condition = imageUrl->Get()->GetStringField(TEXT("condition"));
								TArray<TSharedPtr<FJsonValue>> values = imageUrl->Get()->GetArrayField(TEXT("value"));
								float height = 0.0f;
								int32 index = 0;
								if (condition.FindChar('>', index))
								{
									condition = condition.Right(condition.Len() - index - 1);
									if (condition.FindChar(']', index))
									{
										condition = condition.Left(index);
										height = FCString::Atof(*condition);
									}
								}
								TTuple<float, FString> roof_condition(height, values[0].Get()->AsString());
								building_layer_info.roof_condition.Add(roof_condition);
								TTuple<float, FString> wall_condition(height, values[1].Get()->AsString());
								building_layer_info.wall_condition.Add(wall_condition);
							}
						}
						//增加对象
						TTuple<int32, FGeoBuildingLayerInfo> info(building_layer_info.layer_id, building_layer_info);
						m_building_layer_info.Add(info);
					}
				}
			}
		}
	}

	return true;
}
bool ABuilder::ParseBuildingsJson()
{
	if (m_building_layer_info.Num() == 0)
	{
		UE_LOG(LogClass, Error, TEXT("the buildings layer is null"));
		return false;
	}

	for (auto it = m_building_layer_info.begin(); it != m_building_layer_info.end(); ++it)
	{
		int32 layer_id = it->Value.layer_id;
		FString file_name = m_file_path + it->Value.url;
		TSharedPtr<FJsonObject> rRoot;
		if (!getJsonRootObjectFromFile(file_name, rRoot))
		{
			continue;
		}


		if (rRoot->HasField(TEXT("features")))
		{
			TArray<FBuildingInfo> building_map;
			TArray<TSharedPtr<FJsonValue>> features = rRoot->GetArrayField(TEXT("features"));
			for (int i = 0; i < features.Num(); i++)
			{
				const TSharedPtr<FJsonObject>* feature;
				if (features[i].Get()->TryGetObject(feature))
				{

					//feture type
					FString feature_type = feature->Get()->GetStringField(TEXT("type"));
					if (feature_type != "Feature")
					{
						UE_LOG(LogClass, Error, TEXT("type is not Feature"));
						continue;
					}

					//properties
					TSharedPtr<FJsonObject> properties = feature->Get()->GetObjectField(TEXT("properties"));
					if (properties == nullptr)
					{
						UE_LOG(LogClass, Error, TEXT("properties is null"));
						continue;
					}

					float feature_height = properties->GetNumberField(TEXT("height"));
					float feature_code = properties->GetIntegerField(TEXT("code"));

					//geometries
					TSharedPtr<FJsonObject> geometry = feature->Get()->GetObjectField(TEXT("geometry"));
					FString geometry_type = geometry->GetStringField(TEXT("type"));
					if (geometry_type != "MultiPolygon")
					{
						UE_LOG(LogClass, Error, TEXT("geometry type is not multipolygon"));
						continue;
					}
					TArray<TSharedPtr<FJsonValue>> feature_coordinates = geometry->GetArrayField(TEXT("coordinates"));
					for (TSharedPtr<FJsonValue> feature_coordinate : feature_coordinates)
					{
						FBuildingInfo building;
						building.height = feature_height;
						building.code = feature_code;
						const TArray<TSharedPtr<FJsonValue>> polygon_coordinates = feature_coordinate.Get()->AsArray();
						const TArray<TSharedPtr<FJsonValue>> coordinates = polygon_coordinates[0].Get()->AsArray();
						for (int j = 0; j < coordinates.Num(); j++)
						{
							const TArray<TSharedPtr<FJsonValue>> coordinate = coordinates[j].Get()->AsArray();
							FVector coord;
							coord.X = coordinate[0].Get()->AsNumber();
							coord.Y = coordinate[1].Get()->AsNumber();
							//如果起止点重合，则舍弃终止点
							if (j + 1 == coordinates.Num() && FVector::Dist(building.coords[0], coord) < threshold)
							{
								continue;
							}
							building.coords.Emplace(coord);
						}
						building_map.Add(building);
					}
				}
			}
			TTuple<int32, TArray<FBuildingInfo>> map_info(layer_id, building_map);
			m_building_layer_data.Add(map_info);
		}

	}
	return true;
}
bool ABuilder::getJsonRootObjectFromFile(FString file_name, TSharedPtr<FJsonObject>& json_root)
{
	FString json = "";
	if (!FFileHelper::LoadFileToString(json, *(file_name)) || json == "")
	{
		FString errorMsg = file_name + "加载文件失败.";
		UE_LOG(LogClass, Error, TEXT("===%s==="), *errorMsg);
		return false;
	}

	TSharedRef< TJsonReader<> > Reader = TJsonReaderFactory<>::Create(json);
	if (!FJsonSerializer::Deserialize(Reader, json_root))
	{
		FString errorMsg = file_name + "反序列化失败.";
		UE_LOG(LogClass, Error, TEXT("===%s==="), *errorMsg);
		return false;
	}

	return true;
}

void ABuilder::CreateMesh()
{
	//116.3,40.0--beijing
	//114.3,30.6---
	ProcessCoords(114.3, 30.6);
	FTransform transform;
	CreateWallMesh();
	CreateRoofMesh();
}

void ABuilder::ProcessCoords(double ref_x, double ref_y)
{
	FVector ref_point = Lonlat2Mercator(ref_x, ref_y);
	for (auto it_player = m_building_layer_data.begin(); it_player != m_building_layer_data.end(); ++it_player)
	{
		TArray <FBuildingInfo>& building_data = it_player->Value;
		for (FBuildingInfo& build : building_data)
		{
			int count = build.coords.Num();
			for (int i = 0; i < count; i++)
			{
				build.coords[i] = Lonlat2Mercator(build.coords[i].X, build.coords[i].Y);
				build.coords[i] -= ref_point;
			}
		}
	}
}
FVector ABuilder::Lonlat2Mercator(double lon, double lat, double height)
{
	const double earthRad = 6378137.0;

	FVector mercator;
	double radius = earthRad + height;
	mercator.Y = lon * PI / 180 * radius;
	float alpha = lat * PI / 180;
	mercator.X = radius / 2 * log((1.0 + sin(alpha)) / (1.0 - sin(alpha)));

	return mercator;
}

void ABuilder::CreateWallMesh()
{
	if (m_use_pmc)
	{
		CreateWallMesh_PMCImp();
	}
	else
	{
		CreateWallMesh_RawMeshImp();
	}
}

void ABuilder::CreateWallMesh_PMCImp()
{
	for (auto it_layer_data = m_building_layer_data.begin(); it_layer_data != m_building_layer_data.end(); ++it_layer_data)
	{
		int32 layer_id = it_layer_data->Key;
		TArray<FBuildingInfo> building_data = it_layer_data->Value;
		TArray<FVector> Verties;
		TArray<int32> Index;
		TArray<FVector> Normals;
		TArray<FVector2D> UV;
		TArray<FColor> VertexColors;
		TArray<FProcMeshTangent> Tangents;

		FVector ZUp(0.0, 0.0, 1.0);
		for (FBuildingInfo build : building_data)
		{
			double height = build.height;
			int count = build.coords.Num();
			for (int i = 0; i < count; i++)
			{
				//顶点
				int delta = Verties.Num();
				FVector cur_coord = build.coords[i];
				int32 next_index = i + 1 == count ? 0 : i + 1;
				FVector next_coord = build.coords[next_index];
				Verties.Add(FVector(cur_coord.X, cur_coord.Y, 0));
				Verties.Add(FVector(cur_coord.X, cur_coord.Y, height));
				Verties.Add(FVector(next_coord.X, next_coord.Y, 0));
				Verties.Add(FVector(next_coord.X, next_coord.Y, height));

				//顶点颜色
				VertexColors.Add(FColor(1.0f, 1.0f, 1.0f, 1.0f));
				VertexColors.Add(FColor(1.0f, 1.0f, 1.0f, 1.0f));
				VertexColors.Add(FColor(1.0f, 1.0f, 1.0f, 1.0f));
				VertexColors.Add(FColor(1.0f, 1.0f, 1.0f, 1.0f));

				//发向量
				FVector Forward = next_coord - cur_coord;
				FVector normal = FVector::CrossProduct(ZUp, Forward);
				Normals.Add(normal);
				Normals.Add(normal);
				Normals.Add(normal);
				Normals.Add(normal);

				//纹理坐标
				UV.Add(FVector2D(0.0f, 0.0f));
				UV.Add(FVector2D(0.0f, 1.0f));
				UV.Add(FVector2D(1.0f, 0.0f));
				UV.Add(FVector2D(1.0f, 1.0f));

				//索引			
				int index0 = 0 + delta;
				int index1 = 1 + delta;
				int index2 = 2 + delta;
				int index3 = 3 + delta;
				Index.Add(index0);
				Index.Add(index1);
				Index.Add(index2);
				Index.Add(index1);
				Index.Add(index3);
				Index.Add(index2);
			}
		}

		Tangents.Init(FProcMeshTangent(1.0f, 0.0f, 0.0f), Verties.Num());
		wall_pmc->CreateMeshSection(1, Verties, Index, Normals, UV, UV, UV, UV, VertexColors, Tangents, true);
		wall_pmc->SetCollisionEnabled(ECollisionEnabled::NoCollision);


		FString image_name = "2.png";
		UTexture2D* texture = nullptr;
		int32 width, height;
		if (LoadImageToTexture2D(image_name, texture, width, height))
		{
			UMaterialInterface* Material = CreateMaterial(texture, "wall_material", 0.7, 0.4);
			wall_pmc->SetMaterial(1, Material);
		}
	}
}
void ABuilder::CreateWallMesh_RawMeshImp()
{
	const int32 m_wall_center_random_count = 5;
	FRawMesh TotalRawMesh;
	FRawMesh TopRawMesh;
	FRawMesh BottomRawMesh;
	FRawMesh CenterRawMeshs[m_wall_center_random_count];
	for (auto it_layer_data = m_building_layer_data.begin(); it_layer_data != m_building_layer_data.end(); ++it_layer_data)
	{
		int32 layer_id = it_layer_data->Key;
		TArray<FBuildingInfo> building_data = it_layer_data->Value;
		int32 building_index = 0;
		
		for (FBuildingInfo build : building_data)
		{
		    building_index++;
			if (building_index == m_wall_center_random_count)
			{
				building_index = 0;
			}
			FRawMesh& CenterRawMesh = CenterRawMeshs[building_index];
			double height = build.height;
			int count = build.coords.Num();
			for (int i = 0; i < count; i++)
			{
				FVector cur_coord = build.coords[i];
				int32 next_index = i + 1 == count ? 0 : i + 1;
				FVector next_coord = build.coords[next_index];
				divideRect_RawMeshImp(cur_coord,next_coord,0,height,TotalRawMesh);
				divideRect_RawMeshImp(cur_coord, next_coord, height - m_wall_top_dis, height, TopRawMesh);
				divideRect_RawMeshImp(cur_coord, next_coord, m_wall_bottom_dis, height - m_wall_top_dis, CenterRawMesh);
				divideRect_RawMeshImp(cur_coord, next_coord, 0, m_wall_bottom_dis, BottomRawMesh);
			}
		}
	}

	SaveStaticMeshWithRawMesh("total_wall_mesh","total_wall_material", TotalRawMesh);
	SaveStaticMeshWithRawMesh("top_wall_mesh","top_wall_material", TopRawMesh);
	for (int i = 0; i < m_wall_center_random_count; i++)
	{
		SaveStaticMeshWithRawMesh("center_wall_mesh" + FString::FromInt(i),"ceter_wall_material"+FString::FromInt(i), CenterRawMeshs[i]);
	}
	SaveStaticMeshWithRawMesh("bottom_wall_mesh","bottom_wall_material", BottomRawMesh);
}
void ABuilder::divideRect_RawMeshImp(FVector cur_coord, FVector next_coord, double bottom,double top, FRawMesh& RawMesh )
{
	int delta = RawMesh.VertexPositions.Num();
	RawMesh.VertexPositions.Add(FVector(cur_coord.X, cur_coord.Y, bottom));
	RawMesh.VertexPositions.Add(FVector(cur_coord.X, cur_coord.Y, top));
	RawMesh.VertexPositions.Add(FVector(next_coord.X, next_coord.Y, bottom));
	RawMesh.VertexPositions.Add(FVector(next_coord.X, next_coord.Y, top));

	int index0 = 0 + delta;
	int index1 = 1 + delta;
	int index2 = 2 + delta;
	int index3 = 3 + delta;
	RawMesh.WedgeIndices.Add(index0);
	RawMesh.WedgeIndices.Add(index1);
	RawMesh.WedgeIndices.Add(index2);
	RawMesh.WedgeIndices.Add(index1);
	RawMesh.WedgeIndices.Add(index3);
	RawMesh.WedgeIndices.Add(index2);

	RawMesh.WedgeTexCoords->Add(FVector2D(0.0f, 0.0f));
	RawMesh.WedgeTexCoords->Add(FVector2D(0.0f, 1.0f));
	RawMesh.WedgeTexCoords->Add(FVector2D(1.0f, 0.0f));
	RawMesh.WedgeTexCoords->Add(FVector2D(0.0f, 1.0f));
	RawMesh.WedgeTexCoords->Add(FVector2D(1.0f, 1.0f));
	RawMesh.WedgeTexCoords->Add(FVector2D(1.0f, 0.0f));

	for (int face = 0; face < 2; face++)
	{
		RawMesh.FaceMaterialIndices.Add(0);
		RawMesh.FaceSmoothingMasks.Add(0);
		for (int corner = 0; corner < 3; corner++)
		{
			RawMesh.WedgeTangentX.Add(FVector(1, 0, 0));
			RawMesh.WedgeTangentY.Add(FVector(0, 1, 0));
			RawMesh.WedgeTangentZ.Add(FVector(0, 0, 1));

			RawMesh.WedgeColors.Add(FColor(1.0f, 1.0f, 1.0f, 1.0f));
		}
	}
}

void ABuilder::CreateRoofMesh()
{
	if (m_use_pmc)
	{
		CreateRoofMesh_PMCImp();
	}
	else
	{
		CreateRoofMesh_RawMeshImp();
	}
}
void ABuilder::CreateRoofMesh_PMCImp()
{
	for (auto it_layer_data = m_building_layer_data.begin(); it_layer_data != m_building_layer_data.end(); ++it_layer_data)
	{
		int32 layer_id = it_layer_data->Key;
		TArray<FBuildingInfo> building_data = it_layer_data->Value;
		TArray<FVector> Vertes;
		TArray<int32> Index;
		TArray<FVector> Normals;
		TArray<FVector2D> UV;
		TArray<FColor> VertexColors;
		TArray<FProcMeshTangent> Tangents;
		for (FBuildingInfo build : building_data)
		{
			double height = build.height;
			TArray<FVector> polygon = build.coords;

			if (isConvexPolygon(polygon))
			{
				divideConvexPolygon_PMCImp(polygon, height, Vertes, Index, UV);
			}
			else
			{
				divideConcavePolygon_PMCImp(polygon, height, Vertes, Index, UV);
			}

		}
		VertexColors.Init(FColor(1.0f, 1.0f, 1.0f, 0.5f), Vertes.Num());
		Normals.Init(FVector(0.0, 0.0f, 1.0), Vertes.Num());
		Tangents.Init(FProcMeshTangent(1.0f, 0.0f, 0.0f), Vertes.Num());
		roof_pmc->CreateMeshSection(0, Vertes, Index, Normals, UV, UV, UV, UV, VertexColors, Tangents, true);
		roof_pmc->SetCollisionEnabled(ECollisionEnabled::NoCollision);


		FString image_name = "1.png";
		UTexture2D* texture = nullptr;
		int32 width, height;
		if (LoadImageToTexture2D(image_name, texture, width, height))
		{
			UMaterialInterface* Material = CreateMaterial(texture, "roof_material", 0.7, 0.4);
			roof_pmc->SetMaterial(0, Material);
		}
	}
}
void ABuilder::CreateRoofMesh_RawMeshImp()
{
	FRawMesh RawMesh;
	for (auto it_layer_data = m_building_layer_data.begin(); it_layer_data != m_building_layer_data.end(); ++it_layer_data)
	{
		int32 layer_id = it_layer_data->Key;
		TArray<FBuildingInfo> building_data = it_layer_data->Value;
		for (FBuildingInfo build : building_data)
		{
			double height = build.height;
			TArray<FVector> polygon = build.coords;
			if (isConvexPolygon(polygon))
			{
				divideConvexPolygon_RawMeshImp(polygon, height, RawMesh);
			}
			else
			{
				divideConcavePolygon_RawMeshImp(polygon, height, RawMesh);
			}
		}
	}

	SaveStaticMeshWithRawMesh("roof_mesh","roof_material",RawMesh);
}
void ABuilder::divideConvexPolygon_PMCImp(TArray<FVector> polygon, double height, TArray<FVector>& Vertex, TArray<int32>& Index, TArray<FVector2D>& UV)
{
	int32 count = polygon.Num();
	int delta = Vertex.Num();
	FVector2D min(FLT_MAX, FLT_MAX);
	FVector2D max(FLT_MIN, FLT_MIN);
	for (int i = 0; i < count; i++)
	{
		min.X = polygon[i].X < min.X ? polygon[i].X : min.X;
		min.Y = polygon[i].Y < min.Y ? polygon[i].Y : min.Y;
		max.X = polygon[i].X > max.X ? polygon[i].X : max.X;
		max.Y = polygon[i].Y > max.Y ? polygon[i].Y : max.Y;

		polygon[i].Z = height;
		Vertex.Add(polygon[i]);
	}
	float uv_width = max.X - min.X;
	float uv_height = max.Y - min.Y;

	for (int i = 0; i < count; i++)
	{
		float u = (polygon[i].X - min.X) / uv_width;
		float v = (polygon[i].Y - min.Y) / uv_height;
		UV.Add(FVector2D(u, v));
	}


	for (int i = 0; i < count - 2; i++)
	{
		int index0 = 0 + delta;
		int index1 = 1 + i + delta;
		int index2 = 2 + i + delta;
		Index.Add(index0);
		Index.Add(index2);
		Index.Add(index1);
	}
}
void ABuilder::divideConvexPolygon_RawMeshImp(TArray<FVector> polygon, double height, FRawMesh& RawMesh)
{
	int32 count = polygon.Num();
	int delta = RawMesh.VertexPositions.Num();
	FVector2D min(FLT_MAX, FLT_MAX);
	FVector2D max(FLT_MIN, FLT_MIN);
	for (int i = 0; i < count; i++)
	{
		min.X = polygon[i].X < min.X ? polygon[i].X : min.X;
		min.Y = polygon[i].Y < min.Y ? polygon[i].Y : min.Y;
		max.X = polygon[i].X > max.X ? polygon[i].X : max.X;
		max.Y = polygon[i].Y > max.Y ? polygon[i].Y : max.Y;

		polygon[i].Z = height;
		RawMesh.VertexPositions.Add(polygon[i]);
	}

	float uv_width = max.X - min.X;
	float uv_height = max.Y - min.Y;

	for (int i = 0; i < count - 2; i++)
	{
		int index0 = 0 + delta;
		int index1 = 1 + i + delta;
		int index2 = 2 + i + delta;
		RawMesh.WedgeIndices.Add(index0);
		RawMesh.WedgeIndices.Add(index2);
		RawMesh.WedgeIndices.Add(index1);


		float u0 = (polygon[i].X - min.X) / uv_width;
		float v0 = (polygon[i].Y - min.Y) / uv_height;
		RawMesh.WedgeTexCoords->Add(FVector2D(u0, v0));
		float u2 = (polygon[i + 2].X - min.X) / uv_width;
		float v2 = (polygon[i + 2].Y - min.Y) / uv_height;
		RawMesh.WedgeTexCoords->Add(FVector2D(u2, v2));
		float u1 = (polygon[i + 1].X - min.X) / uv_width;
		float v1 = (polygon[i + 1].Y - min.Y) / uv_height;
		RawMesh.WedgeTexCoords->Add(FVector2D(u1, v1));


		RawMesh.FaceMaterialIndices.Add(0);
		RawMesh.FaceSmoothingMasks.Add(0);
		for (int corner = 0; corner < 3; corner++)
		{
			RawMesh.WedgeTangentX.Add(FVector(1, 0, 0));
			RawMesh.WedgeTangentY.Add(FVector(0, 1, 0));
			RawMesh.WedgeTangentZ.Add(FVector(0, 0, 1));

			RawMesh.WedgeColors.Add(FColor(1.0f, 1.0f, 1.0f, 1.0f));
		}
	
	}
}
void ABuilder::divideConcavePolygon_PMCImp(TArray<FVector> polygon, double height, TArray<FVector>& Vertex, TArray<int32>& Index, TArray<FVector2D>& UV)
{
	int32 count = polygon.Num();
	FVector2D min(FLT_MAX, FLT_MAX);
	FVector2D max(FLT_MIN, FLT_MIN);
	for (int i = 0; i < count; i++)
	{
		min.X = polygon[i].X < min.X ? polygon[i].X : min.X;
		min.Y = polygon[i].Y < min.Y ? polygon[i].Y : min.Y;
		max.X = polygon[i].X > max.X ? polygon[i].X : max.X;
		max.Y = polygon[i].Y > max.Y ? polygon[i].Y : max.Y;

		polygon[i].Z = height;
	}
	float uv_width = max.X - min.X;
	float uv_height = max.Y - min.Y;

	int32 delta = 0;
	int32 index = 0;
	while (count > 3)
	{
		if (isSurplusPoint(polygon, index))//去除共线点
		{
			polygon.RemoveAt(index);
			count = polygon.Num();
			index = 0;
		}
		else if (isDivisiblePoint(polygon, index))
		{
			int pre_index = index == 0 ? count - 1 : index - 1;
			int next_index = index + 1 == count ? 0 : index + 1;

			delta = Vertex.Num();
			Vertex.Add(polygon[pre_index]);
			Vertex.Add(polygon[index]);
			Vertex.Add(polygon[next_index]);

			Index.Add(0 + delta);
			Index.Add(2 + delta);
			Index.Add(1 + delta);


			float pre_u = (polygon[pre_index].X - min.X) / uv_width;
			float pre_v = (polygon[pre_index].Y - min.Y) / uv_height;
			UV.Add(FVector2D(pre_u, pre_v));
			float next_u = (polygon[next_index].X - min.X) / uv_width;
			float next_v = (polygon[next_index].Y - min.Y) / uv_height;
			UV.Add(FVector2D(next_u, next_v));
			float cur_u = (polygon[index].X - min.X) / uv_width;
			float cur_v = (polygon[index].Y - min.Y) / uv_height;
			UV.Add(FVector2D(cur_u, cur_v));


			polygon.RemoveAt(index);
			count = polygon.Num();
			index = 0;
		}
		else
		{
			index++;
		}
	}

	delta = Vertex.Num();
	Vertex.Add(polygon[0]);
	Vertex.Add(polygon[2]);
	Vertex.Add(polygon[1]);


	float pre_u = (polygon[0].X - min.X) / uv_width;
	float pre_v = (polygon[0].Y - min.Y) / uv_height;
	UV.Add(FVector2D(pre_u, pre_v));
	float next_u = (polygon[2].X - min.X) / uv_width;
	float next_v = (polygon[2].Y - min.Y) / uv_height;
	UV.Add(FVector2D(next_u, next_v));
	float cur_u = (polygon[1].X - min.X) / uv_width;
	float cur_v = (polygon[1].Y - min.Y) / uv_height;
	UV.Add(FVector2D(cur_u, cur_v));


	Index.Add(0 + delta);
	Index.Add(2 + delta);
	Index.Add(1 + delta);
}
void ABuilder::divideConcavePolygon_RawMeshImp(TArray<FVector> polygon, double height, FRawMesh& RawMesh)
{
	int32 count = polygon.Num();
	FVector2D min(FLT_MAX, FLT_MAX);
	FVector2D max(FLT_MIN, FLT_MIN);
	for (int i = 0; i < count; i++)
	{
		min.X = polygon[i].X < min.X ? polygon[i].X : min.X;
		min.Y = polygon[i].Y < min.Y ? polygon[i].Y : min.Y;
		max.X = polygon[i].X > max.X ? polygon[i].X : max.X;
		max.Y = polygon[i].Y > max.Y ? polygon[i].Y : max.Y;

		polygon[i].Z = height;
	}
	float uv_width = max.X - min.X;
	float uv_height = max.Y - min.Y;

	int32 delta = 0;
	int32 index = 0;
	while (count > 3)
	{
		if (isSurplusPoint(polygon, index))//去除共线点
		{
			polygon.RemoveAt(index);
			count = polygon.Num();
			index = 0;
		}
		else if (isDivisiblePoint(polygon, index))
		{
			int pre_index = index == 0 ? count - 1 : index - 1;
			int next_index = index + 1 == count ? 0 : index + 1;

			delta = RawMesh.VertexPositions.Num();
			RawMesh.VertexPositions.Add(polygon[pre_index]);
			RawMesh.VertexPositions.Add(polygon[index]);
			RawMesh.VertexPositions.Add(polygon[next_index]);

			RawMesh.WedgeIndices.Add(0 + delta);
			RawMesh.WedgeIndices.Add(2 + delta);
			RawMesh.WedgeIndices.Add(1 + delta);

			float pre_u = (polygon[pre_index].X - min.X) / uv_width;
			float pre_v = (polygon[pre_index].Y - min.Y) / uv_height;
			RawMesh.WedgeTexCoords->Add(FVector2D(pre_u, pre_v));
			float next_u = (polygon[next_index].X - min.X) / uv_width;
			float next_v = (polygon[next_index].Y - min.Y) / uv_height;
			RawMesh.WedgeTexCoords->Add(FVector2D(next_u, next_v));
			float cur_u = (polygon[index].X - min.X) / uv_width;
			float cur_v = (polygon[index].Y - min.Y) / uv_height;
			RawMesh.WedgeTexCoords->Add(FVector2D(cur_u, cur_v));



			RawMesh.FaceMaterialIndices.Add(0);
			RawMesh.FaceSmoothingMasks.Add(0);
			for (int corner = 0; corner < 3; corner++)
			{
				RawMesh.WedgeTangentX.Add(FVector(1, 0, 0));
				RawMesh.WedgeTangentY.Add(FVector(0, 1, 0));
				RawMesh.WedgeTangentZ.Add(FVector(0, 0, 1));

				RawMesh.WedgeColors.Add(FColor(1.0f, 1.0f, 1.0f, 1.0f));
			}
			

			polygon.RemoveAt(index);
			count = polygon.Num();
			index = 0;
		}
		else
		{
			index++;
		}
	}

	delta = RawMesh.VertexPositions.Num();
	RawMesh.VertexPositions.Add(polygon[0]);
	RawMesh.VertexPositions.Add(polygon[1]);
	RawMesh.VertexPositions.Add(polygon[2]);

	RawMesh.WedgeIndices.Add(0 + delta);
	RawMesh.WedgeIndices.Add(2 + delta);
	RawMesh.WedgeIndices.Add(1 + delta);

	float pre_u = (polygon[0].X - min.X) / uv_width;
	float pre_v = (polygon[0].Y - min.Y) / uv_height;
	RawMesh.WedgeTexCoords->Add(FVector2D(pre_u, pre_v));
	float next_u = (polygon[2].X - min.X) / uv_width;
	float next_v = (polygon[2].Y - min.Y) / uv_height;
	RawMesh.WedgeTexCoords->Add(FVector2D(next_u, next_v));
	float cur_u = (polygon[1].X - min.X) / uv_width;
	float cur_v = (polygon[1].Y - min.Y) / uv_height;
	RawMesh.WedgeTexCoords->Add(FVector2D(cur_u, cur_v));


	RawMesh.FaceMaterialIndices.Add(0);
	RawMesh.FaceSmoothingMasks.Add(0);
	for (int corner = 0; corner < 3; corner++)
	{
		RawMesh.WedgeTangentX.Add(FVector(1, 0, 0));
		RawMesh.WedgeTangentY.Add(FVector(0, 1, 0));
		RawMesh.WedgeTangentZ.Add(FVector(0, 0, 1));

		RawMesh.WedgeColors.Add(FColor(1.0f, 1.0f, 1.0f, 1.0f));
	}
	
}

void ABuilder::SaveStaticMeshWithRawMesh(FString MeshName, FString MaterialName, FRawMesh RawMesh)
{
	FString PackageName = "/Game/Mesh/" + MeshName;
	UPackage* MeshPackage = CreatePackage(nullptr, *PackageName);
	UStaticMesh* StaticMesh = NewObject< UStaticMesh >(MeshPackage, FName(*MeshName), RF_Public | RF_Standalone);
	FAssetRegistryModule::AssetCreated(StaticMesh);
	StaticMesh->PreEditChange(nullptr);
	FStaticMeshSourceModel& SrcModel = StaticMesh->AddSourceModel();
	SrcModel.SaveRawMesh(RawMesh);

	//先临时这么读吧
	FString image_name = MaterialName + ".png";
	UTexture2D* texture = nullptr;
	int32 width, height;
	if (LoadImageToTexture2D(image_name, texture, width, height))
	{
	 	UMaterialInterface* Material = CreateMaterial(texture, MaterialName, 0.7, 0.4);
	 	StaticMesh->AddMaterial(Material);
	}
	TArray< FText > BuildErrors;
	StaticMesh->Build(true, &BuildErrors);
	
	StaticMesh->MarkPackageDirty();
	FString PackageFileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	bool Saved = UPackage::SavePackage(MeshPackage, StaticMesh, EObjectFlags::RF_Public | EObjectFlags::RF_Standalone, *PackageFileName);
}

bool ABuilder::LoadImageToTexture2D(const FString& ImageName, UTexture2D*& InTexture, int32& Width, int32& Height)
{
    FString ImagePath = FPaths::ProjectContentDir() + "Image/" + ImageName;
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.FileExists(*ImagePath))
	{
		return false;
	}
	TArray<uint8> ImageResultData;
	FFileHelper::LoadFileToArray(ImageResultData, *ImagePath);
	FString Ex = FPaths::GetExtension(ImagePath, false);
	EImageFormat ImageFormat = EImageFormat::Invalid;
	if (Ex.Equals(TEXT("jpg"), ESearchCase::IgnoreCase) || Ex.Equals(TEXT("jpeg"), ESearchCase::IgnoreCase))
	{
		ImageFormat = EImageFormat::JPEG;
	}
	else if (Ex.Equals(TEXT("png"), ESearchCase::IgnoreCase))
	{
		ImageFormat = EImageFormat::PNG;
	}
	else if (Ex.Equals(TEXT("bmp"), ESearchCase::IgnoreCase))
	{
		ImageFormat = EImageFormat::BMP;
	}
	if (ImageFormat == EImageFormat::Invalid)
	{
		return false;
	}
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
	TSharedPtr<IImageWrapper> ImageWrapperPtr = ImageWrapperModule.CreateImageWrapper(ImageFormat);

	if (ImageWrapperPtr.IsValid() && ImageWrapperPtr->SetCompressed(ImageResultData.GetData(), ImageResultData.Num()))
	{
		int32 index = 0;
		ImageName.FindChar('.', index);
		FString AssetName = ImageName.Left(index);
		FString PackageName = "/Game/Texture/" + AssetName;
		UPackage* Package = CreatePackage(NULL, *PackageName);

		TArray<uint8> OutRawData;
		ImageWrapperPtr->GetRaw(ERGBFormat::BGRA, 8, OutRawData);
		Width = ImageWrapperPtr->GetWidth();
		Height = ImageWrapperPtr->GetHeight();
	
		InTexture = NewObject<UTexture2D>(Package,*AssetName,RF_Standalone | RF_Public);
		FAssetRegistryModule::AssetCreated(InTexture);
		InTexture->PlatformData = new FTexturePlatformData();
		InTexture->PlatformData->SizeX = Width;
		InTexture->PlatformData->SizeY = Height;
		InTexture->PlatformData->PixelFormat = PF_B8G8R8A8;

		FTexture2DMipMap* Mip = new FTexture2DMipMap();
		Mip->SizeX = Width;
		Mip->SizeY = Height;
		Mip->BulkData.Lock(LOCK_READ_WRITE);
		void* TextureData = Mip->BulkData.Realloc(Width * Height * 4);
		FMemory::Memcpy(TextureData, OutRawData.GetData(), OutRawData.Num());
		Mip->BulkData.Unlock();
		InTexture->PlatformData->Mips.Add(Mip);

		InTexture->MipGenSettings = TMGS_NoMipmaps;
		InTexture->Source.Init(Width,Height,1,1,ETextureSourceFormat::TSF_BGRA8, OutRawData.GetData());

		InTexture->UpdateResource();
		InTexture->MarkPackageDirty();
		FString PackageFileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
		bool Saved = UPackage::SavePackage(Package, InTexture, EObjectFlags::RF_Public | EObjectFlags::RF_Standalone, *PackageFileName);
		return Saved;
		
	}
	return false;
}
UMaterialInterface* ABuilder::CreateMaterialInstanceDynamic(UTexture2D* InTexture, float Roughness, float Metallic)
{
	FString path = "/Game/Material/TestMaterial";
	UMaterialInterface* material_interface = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *path));
	UMaterialInstanceDynamic* material_instance_dynamic = UMaterialInstanceDynamic::Create(material_interface, this);
	material_instance_dynamic->BlendMode = BLEND_Opaque;
	material_instance_dynamic->TwoSided = true;
	material_instance_dynamic->SetTextureParameterValue("Texture", InTexture);
	material_instance_dynamic->SetScalarParameterValue("Roughness", Roughness);
	material_instance_dynamic->SetScalarParameterValue("Metallic", Metallic);
	material_instance_dynamic->SetFlags(RF_Standalone | RF_Public);
	material_instance_dynamic->MarkPackageDirty();

	return material_instance_dynamic;
}
UMaterialInterface* ABuilder::CreateMaterial(UTexture2D*& InTexture, FString material_name, float Roughness, float Metallic)
{
	FString PackageName = "/Game/Material/" + material_name;
	UPackage* Package = CreatePackage(NULL, *PackageName);
	//UMaterialFactoryNew* MaterialFactory = NewObject<UMaterialFactoryNew>();
	//UMaterial* material = (UMaterial*)MaterialFactory->FactoryCreateNew(UMaterial::StaticClass(),Package,*material_name,RF_Standalone | RF_Public,nullptr,nullptr);
	UMaterial* material = NewObject<UMaterial>(Package, FName(*material_name), RF_Public | RF_Standalone);
	FAssetRegistryModule::AssetCreated(material);
	
	UMaterialExpressionTextureSample* tex_sample = NewObject<UMaterialExpressionTextureSample>(material);
	tex_sample->Texture = InTexture;
	tex_sample->SamplerType = SAMPLERTYPE_Color;
	material->BaseColor.Expression = tex_sample;
	material->Expressions.Add(tex_sample);

	
 	UMaterialExpressionConstant* opacity = NewObject<UMaterialExpressionConstant>(material);
 	opacity->R = 0.5;
 	material->Opacity.Expression = opacity;
	material->Expressions.Add(opacity);

 	UMaterialExpressionConstant* roughness = NewObject<UMaterialExpressionConstant>(material);
 	roughness->R = Roughness;
 	material->Roughness.Expression = roughness;
	material->Expressions.Add(roughness);

 	UMaterialExpressionConstant* metalness = NewObject<UMaterialExpressionConstant>(material);
 	metalness->R = Metallic;
 	material->Metallic.Expression = metalness;
	material->Expressions.Add(metalness);

	material->BlendMode = BLEND_Opaque;
 	material->TranslucencyLightingMode = TLM_Surface;
 	material->TwoSided = true;
	material->SetFlags(RF_Standalone | RF_Public);
	material->PreEditChange(nullptr);
	material->PostEditChange();
	material->MarkPackageDirty();

	FString PackageFileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	bool Saved = UPackage::SavePackage(Package, InTexture, EObjectFlags::RF_Public | EObjectFlags::RF_Standalone, *PackageFileName);
	
	return material;
}


FRaySegmentCrossType ABuilder::segmentCrossWithYFowardRayWithoutZ(FVector pStart, FVector pEnd, FVector point)
{
	//线段竖直，因为我们认定重合为0个交叉点，所以一定没有交叉点
	if (abs(pStart.X - pEnd.X) < threshold)
	{
		return FRaySegmentCrossType::Cross_None;
	}

	//相交与起点
	if (point.Y < pStart.Y && abs(point.X - pStart.X) <= threshold)
	{
		return FRaySegmentCrossType::Cross_WithStart;
	}

	//相交与终点
	if (point.Y < pStart.Y && abs(point.X - pStart.X) <= threshold)
	{
		return FRaySegmentCrossType::Cross_WithStart;
	}

	//快速筛选
	//则当射线在水平方向上位于两个端点之间，并且射线起点低于线段时，返回true
	if (point.Y < pStart.Y && point.Y < pEnd.Y)
	{
		if ((point.X - pStart.X) * (point.X - pEnd.X) <= threshold)
		{
			return FRaySegmentCrossType::Cross_Nomal;
		}
		else
		{
			return FRaySegmentCrossType::Cross_None;
		}
	}

	//求射线与线段所在直线的交点，如果交点Y值位于线段起止点之间，则表明有交点
	float ratio = (pEnd.Y - pStart.Y) / (pEnd.X - pStart.X);
	float pY = pStart.Y + ratio * (point.X - pStart.X);
	if ((pY - pStart.Y) * (pY - pEnd.Y) <= threshold)
	{
		return FRaySegmentCrossType::Cross_Nomal;
	}
	else
	{
		return FRaySegmentCrossType::Cross_None;
	}
}
bool ABuilder::isSegmentCrossWithoutZ(FVector pStart1, FVector pEnd1, FVector pStart2, FVector pEnd2)
{
	//线段2的起止点是否在线段1的两侧
	FVector P1 = pStart2 - pStart1;
	FVector P2 = pEnd2 - pStart1;
	FVector Q = pEnd1 - pStart1;
	float mark = (P1.X * Q.Y - P1.Y * Q.X) * (P2.X * Q.Y - P2.X * Q.Y);
	if (mark > 0)
	{
		return false;
	}

	//线段1的起止点是否在线段2的两侧
	P1 = pStart1 - pStart2;
	P2 = pEnd1 - pStart2;
	Q = pEnd2 - pStart2;
	mark = (P1.X * Q.Y - P1.Y * Q.X) * (P2.X * Q.Y - P2.X * Q.Y);
	if (mark > 0)
	{
		return false;
	}

	return true;
}
bool ABuilder::pointInPolygon(TArray<FVector> polygon, FVector point)
{
	int cross_count = 0;
	int count = polygon.Num();
	for (int i = 0; i < count; i++)
	{
		int pre_index = i == 0 ? count - 1 : i - 1;
		int start_index = i;
		int end_index = i + 1 == count ? 0 : i + 1;
		int next_index = end_index + 1 == count ? 0 : end_index + 1;
		FVector pre_point = polygon[pre_index];
		FVector start_point = polygon[start_index];
		FVector end_point = polygon[end_index];
		FVector next_point = polygon[next_index];
		FRaySegmentCrossType type = segmentCrossWithYFowardRayWithoutZ(start_point, end_point, point);
		switch (type)
		{
		case FRaySegmentCrossType::Cross_None:
			break;
		case FRaySegmentCrossType::Cross_WithStart:
		{
			//凸点不计算交叉点
			if ((pre_point.X > start_point.X) == (end_point.X > start_point.X))
			{
				continue;
			}
			cross_count++;
		}
		break;
		case FRaySegmentCrossType::Cross_WithEnd:
			//端点只会在起始点记录
			break;
		case FRaySegmentCrossType::Cross_Nomal:
			cross_count++;
			break;
		default:
			break;
		}
	}
	return cross_count % 2 == 0;
}

bool ABuilder::pointRightOfLine(FVector pStart, FVector pEnd, FVector point)
{

	pStart -= point;
	pEnd -= point;

	double mark = pStart.X * pEnd.Y - pStart.Y * pEnd.X;
	return mark < 0.0;
}
bool ABuilder::pointInTriangle(TArray<FVector> triangle, FVector point)
{
	if (triangle.Num() != 3)
	{
		UE_LOG(LogClass, Error, TEXT("三角形顶点数量不正确！！！"));
		return false;
	}

	bool r1 = pointRightOfLine(triangle[0], triangle[1], point);
	bool r2 = pointRightOfLine(triangle[1], triangle[2], point);
	bool r3 = pointRightOfLine(triangle[2], triangle[0], point);
	//顺时针顺序
	if (r1 && r2 && r3)
	{
		return true;
	}
	//逆时针顺序
	if (!r1 && !r2 && !r3)
	{
		return true;
	}

	return false;
}
bool ABuilder::isConvexPoint(TArray<FVector> polygon, int32 index)
{
	int count = polygon.Num();
	int32 pre_index = index == 0 ? count - 1 : index - 1;
	int32 next_index = index + 1 == count ? 0 : index + 1;
	FVector vec1 = polygon[pre_index] - polygon[index];
	FVector vec2 = polygon[next_index] - polygon[index];

	float mark = vec1.X * vec2.Y - vec1.Y * vec2.X;
	return mark < 0.0f;
}
bool ABuilder::isConvexPolygon(TArray<FVector> polygon)
{
	for (int32 i = 0; i < polygon.Num(); i++)
	{
		if (!isConvexPoint(polygon, i))
		{
			return false;
		}
	}
	return true;
}
bool ABuilder::isDivisiblePoint(TArray<FVector> polygon, int32 index)
{
	bool convex = isConvexPoint(polygon, index);
	if (!convex)
	{
		return false;
	}

	int count = polygon.Num();
	int32 pre_index = index == 0 ? count - 1 : index - 1;
	int32 next_index = index + 1 == count ? 0 : index + 1;
	TArray<FVector> triangle;
	triangle.Emplace(polygon[pre_index]);
	triangle.Emplace(polygon[index]);
	triangle.Emplace(polygon[next_index]);
	for (int i = 0; i < count; i++)
	{
		if (i == index || i == pre_index || i == next_index)
		{
			continue;
		}
		if (pointInTriangle(triangle, polygon[i]))
		{
			return false;
		}
	}
	return true;
}
bool ABuilder::isSurplusPoint(TArray<FVector> polygon, int32 index)
{
	int count = polygon.Num();
	int32 pre_index = index == 0 ? count - 1 : index - 1;
	int32 next_index = index + 1 == count ? 0 : index + 1;
	FVector vec1 = polygon[index] - polygon[pre_index];
	FVector vec2 = polygon[next_index] - polygon[index];

	float mark = vec1.X * vec2.Y - vec1.Y * vec2.X;
	return abs(mark) < threshold;
}


