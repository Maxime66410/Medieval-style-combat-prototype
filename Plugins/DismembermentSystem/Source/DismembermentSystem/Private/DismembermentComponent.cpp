// © 2021, Brock Marsh. All rights reserved.

#include "DismembermentComponent.h"

#include "DismembermentSystem_Log.h"
#include "TimerManager.h"
#include "LimbMap.h"
#include "DismemberedAnimInstance.h"
#include "SkeletalRenderPublic.h"
#include "Engine/DataTable.h"
#include "Net/UnrealNetwork.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshRenderData.h"

#if WITH_EDITOR
#include "Animation/AnimBlueprint.h"
#endif

#define AllMeshes for(USkeletalMeshComponent* Mesh : GetAllMeshes()) Mesh

UDismembermentComponent::UDismembermentComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

// Called when the game starts
void UDismembermentComponent::BeginPlay()
{
	Super::BeginPlay();

	SkeletalMeshComponent = Cast<USkeletalMeshComponent>(GetOwner()->GetComponentByClass(USkeletalMeshComponent::StaticClass()));

	if(!SkeletalMeshComponent) DISError("UDismembermentComponent::BeginPlay | The Component cannot find the Owners Skeletal Mesh Component. Please make sure %s has a Skeletal Mesh Component", *GetOwner()->GetName());

	if(!DismemberedAnimInstance) DismemberedAnimInstance = LoadClass<UAnimInstance>(nullptr, TEXT("/DismembermentSystem/UE4_Mannequin_Dismemberment_AnimBP.UE4_Mannequin_Dismemberment_AnimBP_C"));
	
	UpdateLimbMap();
}

void UDismembermentComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UDismembermentComponent, DismemberedBones);
}

USkeletalMesh* UDismembermentComponent::GetMesh() const
{
	return SkeletalMeshComponent->SkeletalMesh;
}

TArray<USkeletalMeshComponent*> UDismembermentComponent::GetAllAttachedMeshes() const
{
	TArray<USceneComponent*> Children;
	SkeletalMeshComponent->GetChildrenComponents(true, Children);
	
	TArray<USkeletalMeshComponent*> ChildMeshes;

	for (USceneComponent* Child : Children)
	{
		if(!Child->IsA(USkeletalMeshComponent::StaticClass())) continue;
		if(Child->ComponentTags.Contains("Dismembered Limb")) continue;
	
		USkeletalMeshComponent* Mesh = Cast<USkeletalMeshComponent>(Child);
		if(Mesh) ChildMeshes.Add(Mesh);
	}

	ChildMeshes.RemoveSwap(SkeletalMeshComponent);

	return ChildMeshes;
}

TArray<USkeletalMeshComponent*> UDismembermentComponent::GetAllAttachedMeshes(USkeletalMeshComponent* Component) const
{
	TArray<USceneComponent*> Children;
	Component->GetChildrenComponents(true, Children);

	TArray<USkeletalMeshComponent*> ChildMeshes;

	for (USceneComponent* Child : Children)
	{
		if(!Child->IsA(USkeletalMeshComponent::StaticClass())) continue;
	
		USkeletalMeshComponent* Mesh = Cast<USkeletalMeshComponent>(Child);
		if(Mesh) ChildMeshes.Add(Mesh);
	}

	return ChildMeshes;
}

TArray<USkeletalMeshComponent*> UDismembermentComponent::GetAllAttachedMeshes(const FName LimbName) const
{
	TArray<USceneComponent*> Children;
	SkeletalMeshComponent->GetChildrenComponents(true, Children);

	TArray<USkeletalMeshComponent*> ChildMeshes;

	for (USceneComponent* Child : Children)
	{
		if(!Child->IsA(USkeletalMeshComponent::StaticClass())) continue;
		// If Attached to a Socket then make sure the socket exist on the limb
		if(Child->GetAttachSocketName() == NAME_None) if(!LimbMap->GetLimb(LimbName).Get().Contains(Child->GetAttachSocketName())) continue;
		
		USkeletalMeshComponent* Mesh = Cast<USkeletalMeshComponent>(Child);
		if(Mesh) ChildMeshes.Add(Mesh);
	}

	return ChildMeshes;
}

TArray<USkeletalMeshComponent*> UDismembermentComponent::GetAllMeshes() const
{
	if(!bSupportAttachedChildMeshes) return {SkeletalMeshComponent};
	
	TArray<USkeletalMeshComponent*> Meshes = GetAllAttachedMeshes();

	if(Meshes.Num() < 1) return {SkeletalMeshComponent};

	Meshes.Insert(SkeletalMeshComponent, 0);

	return Meshes;
}

void UDismembermentComponent::InitVertexColors()
{
	TArray<FLinearColor> Colors;
	Colors.Init(FLinearColor(0,0,0,1), GetVertexNum(SkeletalMeshComponent));
	SkeletalMeshComponent->SetVertexColorOverride_LinearColor(0, Colors);
}

bool UDismembermentComponent::IsLimbMapOutdated() const
{
	if(!LimbMap) return true;
	return LimbMap->Mesh != SkeletalMeshComponent->SkeletalMesh;
}

void UDismembermentComponent::UpdateLimbMap()
{
	if(!SkeletalMeshComponent->SkeletalMesh)
	{
		DISError("Skeletal Mesh is Invalid")
		LimbMap = nullptr;
		return;
	}

	LimbMap = CreateLimbMap();

	DismemberedVertices.SetNumZeroed(LimbMap->VertexNum);
}

ULimbMap* UDismembermentComponent::CreateLimbMap()
{
	ULimbMap* NewMap = NewObject<ULimbMap>(this, ULimbMap::StaticClass(), NAME_None, RF_Transient);

	NewMap->Mesh = GetMesh();
	NewMap->Initialize();

	return NewMap;
}

void UDismembermentComponent::UpdateMissingLimbs(USkeletalMeshComponent* InDismemberedLimb, const FName BoneName)
{
	// Add the Limb to the Missing Limbs map at the end of everything else
	FLimb& Limb = LimbMap->GetLimb(BoneName);
	
	for(FName Bone : Limb.Get())
	{
		// This could have issues with overriding child limbs that are dismembered.
		MissingLimbs.Add(Bone, InDismemberedLimb);
	}
}

void UDismembermentComponent::OnRep_DismemberedBones()
{
	FTimerHandle Handle;
	GetWorld()->GetTimerManager().SetTimer(Handle, this, &UDismembermentComponent::DismemberPingDelayed, 1.f, false);
}

void UDismembermentComponent::DismemberPingDelayed()
{
	for (const FName Bone : GetNetDeltaDismemberedBones())
	{
		DismemberLimb_Internal(Bone, FVector(0));
	}
}

TArray<FName> UDismembermentComponent::GetNetDeltaDismemberedBones()
{
	TArray<FName> Out;
	
	for (FName Bone : DismemberedBones)
	{
		if(!NetDeltaDismemberedBones.Contains(Bone)) Out.Add(Bone);
	}

	return Out;
}

int32 UDismembermentComponent::GetVertexNum(const int32 LOD) const
{
	return GetVertexNum(SkeletalMeshComponent, LOD);
}

int32 UDismembermentComponent::GetVertexNum(const USkeletalMeshComponent* InMesh, const int32 LOD) const
{
	return InMesh->GetSkeletalMeshRenderData()->LODRenderData[LOD].GetNumVertices();
}

void UDismembermentComponent::DismemberLimb(const FName BoneName, const FVector Impulse)
{
	DismemberLimb_Internal(BoneName, Impulse);

	if(!GetIsReplicated()) return;
	if(GetOwner()->HasAuthority())
	{
		DismemberLimb_Multi(BoneName, Impulse);
	}
	else
	{
		DismemberLimb_Server(BoneName, Impulse);
	}
}

void UDismembermentComponent::DismemberLimb_Server_Implementation(const FName BoneName, const FVector& Impulse)
{
	DismemberLimb_Internal(BoneName, Impulse);

	DismemberLimb_Multi(BoneName, Impulse);
}

bool UDismembermentComponent::DismemberLimb_Server_Validate(FName BoneName, const FVector& Impulse)
{
	return true;
}

void UDismembermentComponent::DismemberLimb_Multi_Implementation(const FName BoneName, const FVector& Impulse)
{
	if(GetOwner()->HasAuthority()) return;

	DismemberLimb_Internal(BoneName, Impulse);
}

bool UDismembermentComponent::DismemberLimb_Multi_Validate(FName BoneName, const FVector& Impulse)
{
	return true;
}

void UDismembermentComponent::DismemberLimb_Internal(FName BoneName, const FVector& Impulse)
{
	if(bDisableDismemberment) return;
	if(RemappedBones.Contains(BoneName)) BoneName = *RemappedBones.Find(BoneName);
	if(ExcludedBones.Contains(BoneName)) return;
	if(MissingLimbs.Contains(BoneName)) return;
	if(IsLimbMapOutdated()) UpdateLimbMap();

	PreDismemberment(BoneName, Impulse);

	// Hide the Limb of there Characters Mesh
	AllMeshes->HideBoneByName(BoneName, EPhysBodyOp::PBO_Term);

	/* Break Constraint so that the limbs physics are not being simulated, This can cause weird things like getting an arm caught on something even tho the arm does not exist. */
	AllMeshes->BreakConstraint(FVector(0),FVector(0), BoneName);

	/* Create the New Skeletal Mesh Component for the Dismembered Mesh */
	USkeletalMeshComponent* DismemberedLimb = CreateDismemberedLimb(BoneName);

	/* Set the Anim Instance so that the bones will update */
	SetDismemberedAnimInstance(DismemberedLimb, BoneName);

	/* Removes any limbs that have already been dismembered */
	RemovePreviouslyDismemberedLimbs(DismemberedLimb);

	BeginFrameDelayedDismemberment(DismemberedLimb, BoneName, Impulse);

	DismemberedBones.Add(BoneName);
	NetDeltaDismemberedBones.Add(BoneName);
}

void UDismembermentComponent::DismemberLimbFrameDelayed()
{
	for(FDismemberedLimbFrameDelay& Data : FrameDelayedDismemberedLimbs)
	{
		USkeletalMeshComponent* CachedDismemberedLimb = Data.SkeletalMeshComponent;
		
		CachedDismemberedLimb->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	
		CachedDismemberedLimb->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		CachedDismemberedLimb->SetCollisionObjectType(ECC_PhysicsBody);
		CachedDismemberedLimb->SetCollisionResponseToAllChannels(ECR_Ignore);
		CachedDismemberedLimb->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
		CachedDismemberedLimb->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);

		CreateDismemberPhysicsAsset(CachedDismemberedLimb, Data.BoneName);
		
		CachedDismemberedLimb->SetSimulatePhysics(true);
		
		CachedDismemberedLimb->AddImpulse(Data.Impulse, Data.BoneName, true);
		
		UpdateMissingLimbs(CachedDismemberedLimb, Data.BoneName);
		
		PostDismemberment(Data.BoneName, CachedDismemberedLimb);
	}

	FrameDelayedDismemberedLimbs.Reset();
}

void UDismembermentComponent::SetDismemberedAnimInstance(USkeletalMeshComponent* Component, FName BoneName) const
{
	Component->SetAnimInstanceClass(DismemberedAnimInstance);
	UDismemberedAnimInstance* AnimInstance = Cast<UDismemberedAnimInstance>(Component->GetAnimInstance());
	if(AnimInstance) AnimInstance->Limb = BoneName;
}

void UDismembermentComponent::RemovePreviouslyDismemberedLimbs(USkeletalMeshComponent* Component)
{
		/* Hides already dismembered limbs. For Example, Dismembering a Hand then the Arm. The New Arm should not have the previously removed hand. */
	for(const TTuple<FName, USkeletalMeshComponent*>& Child : MissingLimbs)
	{
		Component->HideBoneByName(Child.Key, EPhysBodyOp::PBO_Term);
	}
}

void UDismembermentComponent::CreateDismemberPhysicsAsset(USkeletalMeshComponent* Component, FName InLimb) const
{
	UPhysicsAsset* NewPhysicsAsset = DuplicateObject<UPhysicsAsset>(Component->GetPhysicsAsset(), Component);
	
	TArray<FName> BoneNames;
	Component->GetBoneNames(BoneNames);
	for(const FName Bone : BoneNames)
	{
		const int32 Index = NewPhysicsAsset->FindBodyIndex(Bone);
	
		if(Index == INDEX_NONE) continue;
		if(LimbMap->GetLimb(InLimb).Get().Contains(Bone)) continue;

		TerminatePhysicsBodies(NewPhysicsAsset, Index); // <--- From: FPhysicsAssetUtils::DestroyBody(NewPhysicsAsset, Index);
	}
	
	Component->SetPhysicsAsset(NewPhysicsAsset, true);
}

void UDismembermentComponent::TerminatePhysicsBodies(UPhysicsAsset* PhysicsAsset, int32 Index) const
{
	check(PhysicsAsset);
	
	// First we must correct the CollisionDisableTable.
	// All elements which refer to bodyIndex are removed.
	// All elements which refer to a body with index >bodyIndex are adjusted. 

	TMap<FRigidBodyIndexPair,bool> NewCDT;
	for(int32 i=1; i< PhysicsAsset->SkeletalBodySetups.Num(); i++)
	{
		for(int32 j=0; j<i; j++)
		{
			FRigidBodyIndexPair Key(j,i);

			// If there was an entry for this pair, and it doesn't refer to the removed body, we need to add it to the new CDT.
			if( PhysicsAsset->CollisionDisableTable.Find(Key) )
			{
				if(i != Index && j != Index)
				{
					int32 NewI = (i > Index) ? i-1 : i;
					int32 NewJ = (j > Index) ? j-1 : j;

					FRigidBodyIndexPair NewKey(NewJ, NewI);
					NewCDT.Add(NewKey, 0);
				}
			}
		}
	}

	PhysicsAsset->CollisionDisableTable = NewCDT;

	// Now remove any constraints that were attached to this body.
	// This is a bit yuck and slow...
	TArray<int32> Constraints;
	PhysicsAsset->BodyFindConstraints(Index, Constraints);

	while(Constraints.Num() > 0)
	{
		PhysicsAsset->ConstraintSetup.RemoveAt(Constraints[0]);
		PhysicsAsset->BodyFindConstraints(Index, Constraints);
	}

	// Remove pointer from array. Actual objects will be garbage collected.
	PhysicsAsset->SkeletalBodySetups.RemoveAt(Index);

	PhysicsAsset->UpdateBodySetupIndexMap();
	// Update body indices.
	PhysicsAsset->UpdateBoundsBodiesArray();
}

FBodyInstance* UDismembermentComponent::GetBodyInstance(USkeletalMeshComponent* Component, FName BoneName)
{
	for(int32 i = 0; i < Component->Bodies.Num(); i++)
	{
		FBodyInstance* Body = Component->Bodies[i];
		
		if(BoneName == Body->BodySetup->BoneName) return Body;
	}

	return nullptr;
}

USkeletalMeshComponent* UDismembermentComponent::CreateDismemberedLimb(const FName BoneName)
{
	USkeletalMeshComponent* DisLimb = NewObject<USkeletalMeshComponent>(GetOwner());

	DisLimb->ComponentTags.Add("Dismembered Limb");
	
	DisLimb->AttachToComponent(SkeletalMeshComponent, FAttachmentTransformRules::SnapToTargetIncludingScale, BoneName);
	
	DisLimb->RegisterComponent();

	// Copy Mesh
	DisLimb->SetSkeletalMesh(SkeletalMeshComponent->SkeletalMesh);
	
	// Copy Materials
	for(int32 i = 0; i < SkeletalMeshComponent->GetNumMaterials(); i++)
	{
		DisLimb->SetMaterial(i, SkeletalMeshComponent->GetMaterial(i));
	}

	DisLimb->bReceivesDecals = SkeletalMeshComponent->bReceivesDecals;
	
	// Copy Physics Asset
	DisLimb->SetPhysicsAsset(SkeletalMeshComponent->GetPhysicsAsset());

	CopyVertexColorsToMesh(SkeletalMeshComponent, DisLimb);

	if(bSupportAttachedChildMeshes)
	{
		// Create Mesh for every Attached Mesh
		for (USkeletalMeshComponent* Mesh : GetAllAttachedMeshes())
		{
			if(Mesh->ComponentTags.Contains("No Dismemberment")) continue;
			// If its a mesh that has an attached socket, then make sure the meshes socket is attached to this limb
			if(Mesh->GetAttachSocketName() != NAME_None && !LimbMap->GetLimb(BoneName).Get().Contains(Mesh->GetAttachSocketName())) continue;

			// If Mesh is meant to be transferred, then dont create a new mesh. (Ex. Guns, Swords, etc.)
			if(Mesh->ComponentTags.Contains("Transfer Dismemberment"))
			{
				Mesh->AttachToComponent(DisLimb, FAttachmentTransformRules::SnapToTargetIncludingScale, Mesh->GetAttachSocketName());

				continue;
			}
		
			// Copy the Mesh so that we can remove its limbs. Else, Its an attached item such as a gun or sword.
			USkeletalMeshComponent* Child = NewObject<USkeletalMeshComponent>(GetOwner());

			Child->ComponentTags.Add("Dismembered Limb");
		
			Child->AttachToComponent(DisLimb, FAttachmentTransformRules::SnapToTargetIncludingScale, Mesh->GetAttachSocketName());

			// Dont Initialize a Mesh that is being transferred
			Child->RegisterComponent();
		
			// Copy Mesh
			Child->SetSkeletalMesh(Mesh->SkeletalMesh);
	
			// Copy Materials
			for(int32 i = 0; i < Mesh->GetNumMaterials(); i++)
			{
				Child->SetMaterial(i, Mesh->GetMaterial(i));
			}

			Child->bReceivesDecals = Mesh->bReceivesDecals;
		
			Child->SetMasterPoseComponent(DisLimb);

			CopyVertexColorsToMesh(Mesh, Child);
		}
	}

	return DisLimb;
}

void UDismembermentComponent::DestroyLimb(const FName BoneName)
{
	/* Scales the Bone and its child bones down to 0 */
	if(bSupportAttachedChildMeshes)
	{
		AllMeshes->HideBoneByName(BoneName, EPhysBodyOp::PBO_Term);
	}
	else
	{
		SkeletalMeshComponent->HideBoneByName(BoneName, EPhysBodyOp::PBO_Term);
	}
}

void UDismembermentComponent::BeginFrameDelayedDismemberment(USkeletalMeshComponent* Component, const FName BoneName, const FVector Impulse)
{
	FDismemberedLimbFrameDelay Data;
	Data.SkeletalMeshComponent = Component;
	Data.BoneName = BoneName;
	Data.Impulse = Impulse;

	FrameDelayedDismemberedLimbs.Add(Data);
	
	GetWorld()->GetTimerManager().SetTimerForNextTick(this, &UDismembermentComponent::DismemberLimbFrameDelayed);
}

void UDismembermentComponent::GetDismemberAlphas(const FName BoneName, TArray<FLinearColor>& Colors, TArray<FLinearColor>& Inverted)
{
	TArray<float> VertexAlpha = LimbMap->GetMaskOfImmediateLimb(BoneName, 0.1f, false);
	
	Colors.SetNumUninitialized(VertexAlpha.Num());
	Inverted.SetNumUninitialized(VertexAlpha.Num());

	for(int32 i = 0; i < VertexAlpha.Num(); i++)
	{
		FLinearColor Color = SkeletalMeshComponent->GetVertexColor(i);
		Color.A = VertexAlpha[i];

		Colors[i] = Color;
		
		Color.A = (1 - Color.A) - DismemberedVertices[i].A;
		Inverted[i] = Color;
	}
}

TArray<float> UDismembermentComponent::CombineDismemberVertices(const TArray<float>& V1, const TArray<float>& V2)
{
	TArray<float> Out;

	for(int32 i = 0; i < V1.Num(); i++)
	{
		Out[i] = V1[i] > 0 ? V1[i] : V2[i];
	}

	return Out;
}

TArray<FLinearColor> UDismembermentComponent::CombineDismemberVertices(const TArray<FLinearColor>& V1, const TArray<FLinearColor>& V2, const EDismemberColorChannel Channel)
{
	TArray<FLinearColor> Out;
	Out.SetNumZeroed(V1.Num());

	for(int32 i = 0; i < V1.Num(); i++)
	{
		Out[i] = GetColorOfChannel(V1[i], Channel) > 0 ? V1[i] : V2[i];
	}

	return Out;
}

FLinearColor UDismembermentComponent::GetCurrentVertexColor(const USkeletalMeshComponent* Mesh, const int32 VertexIndex, const int32 LOD) const
{
	// Fail if no mesh or no color vertex buffer.
	FColor FallbackColor = FColor(255, 255, 255, 255);
	if (!Mesh->SkeletalMesh || !Mesh->MeshObject)
	{
		return FallbackColor.ReinterpretAsLinear();
	}

	// If there is an override, return that
	if (Mesh->LODInfo.Num() > 0 &&
		Mesh->LODInfo.IsValidIndex(LOD) &&
		Mesh->LODInfo[LOD].OverrideVertexColors != nullptr && 
		Mesh->LODInfo[LOD].OverrideVertexColors->IsInitialized() &&
		VertexIndex < (int32)Mesh->LODInfo[LOD].OverrideVertexColors->GetNumVertices() )
	{
		return Mesh->LODInfo[LOD].OverrideVertexColors->VertexColor(VertexIndex).ReinterpretAsLinear();
	}

	FSkeletalMeshLODRenderData& LODData = Mesh->MeshObject->GetSkeletalMeshRenderData().LODRenderData[LOD];
	
	if (!LODData.StaticVertexBuffers.ColorVertexBuffer.IsInitialized())
	{
		return FallbackColor.ReinterpretAsLinear();
	}

	// Find the chunk and vertex within that chunk, and skinning type, for this vertex.
	int32 SectionIndex;
	int32 VertIndex;
	LODData.GetSectionFromVertexIndex(VertexIndex, SectionIndex, VertIndex);

	check(SectionIndex < LODData.RenderSections.Num());
	const FSkelMeshRenderSection& Section = LODData.RenderSections[SectionIndex];
	
	int32 VertexBase = Section.BaseVertexIndex;

	return LODData.StaticVertexBuffers.ColorVertexBuffer.VertexColor(VertexBase + VertIndex).ReinterpretAsLinear();
}

TArray<FLinearColor> UDismembermentComponent::GetCurrentVertexColors(const USkeletalMeshComponent* Mesh)
{
	return GetCurrentVertexColors(Mesh, 0);
}

TArray<FLinearColor> UDismembermentComponent::FColorArrayToLinear(const TArray<FColor>& Colors) const
{
	TArray<FLinearColor> LinearColors;
	LinearColors.SetNum(Colors.Num());

	for (int32 i = 0; i < Colors.Num(); ++i)
	{
		LinearColors[i] = Colors[i].ReinterpretAsLinear();
	}
	
	return LinearColors;
}

float UDismembermentComponent::GetColorOfChannel(const FLinearColor& Color, const EDismemberColorChannel Channel)
{
	switch (Channel)
	{
	case EDismemberColorChannel::R_Channel:
		{
			return Color.R;
		}
	case EDismemberColorChannel::G_Channel:
		{
			return Color.G;
		}
	case EDismemberColorChannel::B_Channel:
		{
			return Color.B;
		}
	case EDismemberColorChannel::A_Channel:
		{
			return Color.A;
		}
	}

	return 0;
}

void UDismembermentComponent::SetColorOfChannel(FLinearColor& Color, const float Value, const EDismemberColorChannel Channel)
{
	switch (Channel)
	{
	case EDismemberColorChannel::R_Channel:
		{
			Color.R = Value;
			return;
		}
	case EDismemberColorChannel::G_Channel:
		{
			Color.G = Value;
			return;
		}
	case EDismemberColorChannel::B_Channel:
		{
			Color.B = Value;
			return;
		}
	case EDismemberColorChannel::A_Channel:
		{
			Color.A = Value;
			return;
		}
	}
}

void UDismembermentComponent::AddColorOfChannel(FLinearColor& Color, float Value, EDismemberColorChannel Channel)
{
	switch (Channel)
	{
	case EDismemberColorChannel::R_Channel:
		{
			Color.R += Value;
			return;
		}
	case EDismemberColorChannel::G_Channel:
		{
			Color.G += Value;
			return;
		}
	case EDismemberColorChannel::B_Channel:
		{
			Color.B += Value;
			return;
		}
	case EDismemberColorChannel::A_Channel:
		{
			Color.A += Value;
			return;
		}
	}
}

void UDismembermentComponent::MaxColorOfChannel(FLinearColor& Color, float Value, EDismemberColorChannel Channel)
{
	switch (Channel)
	{
	case EDismemberColorChannel::R_Channel:
		{
			Color.R = FMath::Max(Color.R, Value);
			return;
		}
	case EDismemberColorChannel::G_Channel:
		{
			Color.G = FMath::Max(Color.G, Value);
			return;
		}
	case EDismemberColorChannel::B_Channel:
		{
			Color.B = FMath::Max(Color.B, Value);
			return;
		}
	case EDismemberColorChannel::A_Channel:
		{
			Color.A = FMath::Max(Color.A, Value);
			return;
		}
	}
}

void UDismembermentComponent::MinColorOfChannel(FLinearColor& Color, float Value, EDismemberColorChannel Channel)
{
	switch (Channel)
	{
	case EDismemberColorChannel::R_Channel:
		{
			Color.R = FMath::Min(Color.R, Value);
			return;
		}
	case EDismemberColorChannel::G_Channel:
		{
			Color.G = FMath::Min(Color.G, Value);
			return;
		}
	case EDismemberColorChannel::B_Channel:
		{
			Color.B = FMath::Min(Color.B, Value);
			return;
		}
	case EDismemberColorChannel::A_Channel:
		{
			Color.A = FMath::Min(Color.A, Value);
			return;
		}
	}
}

void UDismembermentComponent::InvertLinearColorAlpha(TArray<FLinearColor>& Colors)
{
	for(int32 i = 0; i < Colors.Num(); i++)
	{
		Colors[i].A = 1 - Colors[i].A;
	}
}

void UDismembermentComponent::SetLinearColorChannel(TArray<FLinearColor>& Colors, TArray<float>& Values, EDismemberColorChannel Channel)
{
	for(int32 i = 0; i < Colors.Num(); i++)
	{
		SetColorOfChannel(Colors[i], Values[i], Channel);
	}
}

void UDismembermentComponent::SetLinearColorChannel(TArray<FLinearColor>& Colors, const float Value, const EDismemberColorChannel Channel)
{
	for(int32 i = 0; i < Colors.Num(); i++)
	{
		SetColorOfChannel(Colors[i], Value, Channel);
	}
}

void UDismembermentComponent::AddLinearColorChannel(TArray<FLinearColor>& Colors, TArray<float>& Values, const EDismemberColorChannel Channel)
{
	for(int32 i = 0; i < Colors.Num(); i++)
	{
		AddColorOfChannel(Colors[i], Values[i], Channel);
	}
}

void UDismembermentComponent::MaxLinearColorChannel(TArray<FLinearColor>& Colors, TArray<float>& Values, const EDismemberColorChannel Channel)
{
	for(int32 i = 0; i < Colors.Num(); i++)
	{
		MaxColorOfChannel(Colors[i], Values[i], Channel);
	}
}

void UDismembermentComponent::MinLinearColorChannel(TArray<FLinearColor>& Colors, TArray<float>& Values, const EDismemberColorChannel Channel)
{
	for(int32 i = 0; i < Colors.Num(); i++)
	{
		MinColorOfChannel(Colors[i], Values[i], Channel);
	}
}

void UDismembermentComponent::InvertArrayValues(TArray<float>& Values)
{
	for(int32 i = 0; i < Values.Num(); i++)
	{
		Values[i] = 1 - Values[i];
	}
}

void UDismembermentComponent::ApplyMaskedVertexColors(const TArray<FLinearColor>& VertexColors) const
{
	SkeletalMeshComponent->SetVertexColorOverride_LinearColor(0, VertexColors);
}

TArray<FLinearColor> UDismembermentComponent::GetCurrentVertexColors(const int32 LODIndex)
{
	return GetCurrentVertexColors(SkeletalMeshComponent, LODIndex);
}

TArray<FLinearColor> UDismembermentComponent::GetCurrentVertexColors(const USkeletalMeshComponent* Mesh, const int32 LODIndex) const
{
	TArray<FLinearColor> Out;
	Out.SetNumUninitialized(GetVertexNum(Mesh, LODIndex));

	for(int32 i = 0; i < GetVertexNum(Mesh, LODIndex); i++)
	{
		Out[i] = GetCurrentVertexColor(Mesh, i, LODIndex);
	}

	return Out;
}

void UDismembermentComponent::CopyVertexColorsToMesh(const USkeletalMeshComponent* FromMesh, USkeletalMeshComponent* ToMesh)
{
	int32 NumLODs = FromMesh->GetSkeletalMeshRenderData()->LODRenderData.Num();
	
	for (int32 LOD = 0; LOD < NumLODs; ++LOD)
	{
		TArray<FLinearColor> Colors = GetCurrentVertexColors(FromMesh, LOD);

		ToMesh->SetVertexColorOverride_LinearColor(LOD, Colors);
	}
}

void UDismembermentComponent::PreDismemberment(const FName BoneName, FVector Impulse)
{
	OnPreDismemberment.Broadcast(BoneName, Impulse);
}

void UDismembermentComponent::PostDismemberment(const FName BoneName, USkeletalMeshComponent* DismemberMesh)
{
	OnPostDismemberment.Broadcast(BoneName, DismemberMesh);
}

#if WITH_EDITOR
void UDismembermentComponent::SetAnimInstanceTargetSkeleton(UObject* InAnimInstance, USkeleton* InSkeleton)
{
	UAnimBlueprint* Blueprint = Cast<UAnimBlueprint>(InAnimInstance);

	if(!Blueprint) return;

	Blueprint->Modify();

	Blueprint->TargetSkeleton = InSkeleton;
}
#endif