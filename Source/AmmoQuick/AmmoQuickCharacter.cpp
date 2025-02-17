// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AmmoQuickCharacter.h"
#include "AmmoQuickProjectile.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/InputSettings.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "MotionControllerComponent.h"
#include "XRMotionControllerBase.h" // for FXRMotionControllerBase::RightHandSourceId
#include "Runtime/Engine/Public/TimerManager.h"
#include "Runtime/Engine/Classes/GameFramework/CharacterMovementComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Sound/SoundCue.h"
//#include "Engine.h"

DEFINE_LOG_CATEGORY_STATIC(LogFPChar, Warning, All);

//////////////////////////////////////////////////////////////////////////
// AAmmoQuickCharacter

AAmmoQuickCharacter::AAmmoQuickCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(55.f, 96.0f);

	// set our turn rates for input
	BaseTurnRate = 45.f;
	BaseLookUpRate = 45.f;

	// Create a CameraComponent	
	FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCameraComponent->SetupAttachment(GetCapsuleComponent());
	FirstPersonCameraComponent->SetRelativeLocation(FVector(-39.56f, 1.75f, 64.f)); // Position the camera
	FirstPersonCameraComponent->bUsePawnControlRotation = true;

	// Create a mesh component that will be used when being viewed from a '1st person' view (when controlling this pawn)
	Mesh1P = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CharacterMesh1P"));
	Mesh1P->SetOnlyOwnerSee(true);
	Mesh1P->SetupAttachment(FirstPersonCameraComponent);
	Mesh1P->bCastDynamicShadow = false;
	Mesh1P->CastShadow = false;
	Mesh1P->SetRelativeRotation(FRotator(1.9f, -19.19f, 5.2f));
	Mesh1P->SetRelativeLocation(FVector(-0.5f, -4.4f, -155.7f));

	// Create a gun mesh component
	FP_Gun = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FP_Gun"));
	FP_Gun->SetOnlyOwnerSee(true);			// only the owning player will see this mesh
	FP_Gun->bCastDynamicShadow = false;
	FP_Gun->CastShadow = false;
	// FP_Gun->SetupAttachment(Mesh1P, TEXT("GripPoint"));
	FP_Gun->SetupAttachment(RootComponent);

	FP_MuzzleLocation = CreateDefaultSubobject<USceneComponent>(TEXT("MuzzleLocation"));
	FP_MuzzleLocation->SetupAttachment(FP_Gun);
	FP_MuzzleLocation->SetRelativeLocation(FVector(0.2f, 48.4f, -10.6f));

	// Default offset from the character location for projectiles to spawn
	GunOffset = FVector(100.0f, 0.0f, 10.0f);

	// Note: The ProjectileClass and the skeletal mesh/anim blueprints for Mesh1P, FP_Gun, and VR_Gun 
	// are set in the derived blueprint asset named MyCharacter to avoid direct content references in C++.

	// Create VR Controllers.
	R_MotionController = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("R_MotionController"));
	R_MotionController->MotionSource = FXRMotionControllerBase::RightHandSourceId;
	R_MotionController->SetupAttachment(RootComponent);
	L_MotionController = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("L_MotionController"));
	L_MotionController->SetupAttachment(RootComponent);

	// Create a gun and attach it to the right-hand VR controller.
	// Create a gun mesh component
	VR_Gun = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("VR_Gun"));
	VR_Gun->SetOnlyOwnerSee(true);			// only the owning player will see this mesh
	VR_Gun->bCastDynamicShadow = false;
	VR_Gun->CastShadow = false;
	VR_Gun->SetupAttachment(R_MotionController);
	VR_Gun->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));

	VR_MuzzleLocation = CreateDefaultSubobject<USceneComponent>(TEXT("VR_MuzzleLocation"));
	VR_MuzzleLocation->SetupAttachment(VR_Gun);
	VR_MuzzleLocation->SetRelativeLocation(FVector(0.000004, 53.999992, 10.000000));
	VR_MuzzleLocation->SetRelativeRotation(FRotator(0.0f, 90.0f, 0.0f));		// Counteract the rotation of the VR gun model.

	// Uncomment the following line to turn motion controllers on by default:
	//bUsingMotionControllers = true;

	static ConstructorHelpers::FObjectFinder<USoundCue> ReloadSoundObj(TEXT("/Game/FirstPerson/Audio/Reload_Cue"));
	if (ReloadSoundObj.Succeeded())
	{
		ReloadSound = ReloadSoundObj.Object;
	}

	static ConstructorHelpers::FObjectFinder<USoundCue> FuelUseSoundObj(TEXT("/Game/FirstPerson/Audio/FuelUse_Cue"));
	if (FuelUseSoundObj.Succeeded())
	{
		FuelUseSound = FuelUseSoundObj.Object;
	}


	clipSize = 10;
	maxAmmo = 30;
	
	recoilRate = -1.75f;
	FireRate = .75f;
	
	WalkSpeed = 600.f;
	SprintSpeed = 1000.f;
	

	MaxStamina = 100.f;
	
	SprintStaminaRate = .2f;
	SprintStaminaUsage = 5.f;
	bSprinting = false;
	
	StaminaRecoveryRate = .4f;
	StaminaRecoveryMagnitude = 10.f;
	

	JumpHeight = 600.f;
	
	bCanWarp = true;
	WarpDistance = 6000.f;
	WarpCooldown = 1.f;
	WarpStop = 0.1f;

	MaxFuel = 50.f;
	DoubleJumpFuelConsumption = 5.f;
	WarpFuelConsumption = 10.f;

	bCanDash = true;
	DashCooldown = .6f;
	DashDistance = 3000.f;
	DashStaminaUsage = 20.f;

	clip = clipSize;
	ammo = maxAmmo;
	Stamina = MaxStamina;
	Fuel = MaxFuel;

	StaminaLeft = Stamina;
	FuelLeft = Fuel;
}

void AAmmoQuickCharacter::BeginPlay()
{
	// Call the base class  
	Super::BeginPlay();

	//Attach gun mesh component to Skeleton, doing it here because the skeleton is not yet created in the constructor
	FP_Gun->AttachToComponent(Mesh1P, FAttachmentTransformRules(EAttachmentRule::SnapToTarget, true), TEXT("GripPoint"));

	// Show or hide the two versions of the gun based on whether or not we're using motion controllers.
	if (bUsingMotionControllers)
	{
		VR_Gun->SetHiddenInGame(false, true);
		Mesh1P->SetHiddenInGame(true, true);
	}
	else
	{
		VR_Gun->SetHiddenInGame(true, true);
		Mesh1P->SetHiddenInGame(false, true);
	}
}

//////////////////////////////////////////////////////////////////////////
// Input

void AAmmoQuickCharacter::SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent)
{
	// set up gameplay key bindings
	check(PlayerInputComponent);

	// Bind jump events
	PlayerInputComponent->BindAction("Jump", IE_Pressed, this, &AAmmoQuickCharacter::DoubleJump);
	//PlayerInputComponent->BindAction("Jump", IE_Released, this, &ACharacter::StopJumping);

	// Bind sprint events
	PlayerInputComponent->BindAction("Sprint", IE_Pressed, this, &AAmmoQuickCharacter::Sprint);
	PlayerInputComponent->BindAction("Sprint", IE_Released, this, &AAmmoQuickCharacter::Walk);

	// Bind dash event
	PlayerInputComponent->BindAction("Dash", IE_Pressed, this, &AAmmoQuickCharacter::Dash);

	// Bind Warp event
	PlayerInputComponent->BindAction("Warp", IE_Pressed, this, &AAmmoQuickCharacter::Warp);

	// Bind fire event
	PlayerInputComponent->BindAction("Fire", IE_Pressed, this, &AAmmoQuickCharacter::AutoFire);
	PlayerInputComponent->BindAction("Fire", IE_Released, this, &AAmmoQuickCharacter::StopAutoFire);
	
	// Bind Reload Event
	PlayerInputComponent->BindAction("Reload", IE_Pressed, this, &AAmmoQuickCharacter::ReloadClip);

	// Enable touchscreen input
	EnableTouchscreenMovement(PlayerInputComponent);

	PlayerInputComponent->BindAction("ResetVR", IE_Pressed, this, &AAmmoQuickCharacter::OnResetVR);

	// Bind movement events
	PlayerInputComponent->BindAxis("MoveForward", this, &AAmmoQuickCharacter::MoveForward);
	PlayerInputComponent->BindAxis("MoveRight", this, &AAmmoQuickCharacter::MoveRight);

	// We have 2 versions of the rotation bindings to handle different kinds of devices differently
	// "turn" handles devices that provide an absolute delta, such as a mouse.
	// "turnrate" is for devices that we choose to treat as a rate of change, such as an analog joystick
	PlayerInputComponent->BindAxis("Turn", this, &APawn::AddControllerYawInput);
	PlayerInputComponent->BindAxis("TurnRate", this, &AAmmoQuickCharacter::TurnAtRate);
	PlayerInputComponent->BindAxis("LookUp", this, &APawn::AddControllerPitchInput);
	PlayerInputComponent->BindAxis("LookUpRate", this, &AAmmoQuickCharacter::LookUpAtRate);
}

void AAmmoQuickCharacter::DoubleJump()
{
	if (DoubleJumpCounter == 0 || (DoubleJumpCounter == 1 && Fuel >= DoubleJumpFuelConsumption))
	{
		if (DoubleJumpCounter == 1)
		{
			FuelLeft -= DoubleJumpFuelConsumption;
			if (FuelUseSound)
			{
				UGameplayStatics::PlaySoundAtLocation(GetWorld(), FuelUseSound, GetActorLocation());
			}
		}
		ACharacter::LaunchCharacter(FVector(0.f, 0.f, JumpHeight), false, true);
		DoubleJumpCounter++;
		bCanDash = false;
	}
}

void AAmmoQuickCharacter::Landed(const FHitResult& Hit)
{
	if (DoubleJumpCounter)
	{
		bCanDash = true;
	}
	DoubleJumpCounter = 0;
}

bool AAmmoQuickCharacter::IsPlayerMovingForward()
{
	FVector Vel = GetVelocity();
	
	FVector Forward = GetActorForwardVector();
	int ForwardSpeed = FVector::DotProduct(Vel, Forward);

	// FVector Right = GetActorRightVector();
	// int RightSpeed = FVector::DotProduct(Vel, Right);
	
	// if (RightSpeed == 0 && ForwardSpeed > 0)
	if (ForwardSpeed > 0.f)
	{
		return true;
	}
	return false;
}

void AAmmoQuickCharacter::Sprint()
{
	// if(!bSprinting)
	if (IsPlayerMovingForward() && DoubleJumpCounter == 0)
	{
		GetCharacterMovement()->MaxWalkSpeed = SprintSpeed;
		GetWorldTimerManager().ClearTimer(StaminaHandle);
		GetWorldTimerManager().SetTimer(StaminaHandle, this, &AAmmoQuickCharacter::SprintingStamina, SprintStaminaRate, true);
		bSprinting = true;
	}
}

void AAmmoQuickCharacter::SprintingStamina()
{
	if (StaminaLeft >= SprintStaminaUsage)
	{
		StaminaLeft -= SprintStaminaUsage;
	}
	else
	{
		Walk();
	}
}

void AAmmoQuickCharacter::Walk()
{
	if (bSprinting)
	{
		GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
		GetWorldTimerManager().ClearTimer(StaminaHandle);
		GetWorldTimerManager().SetTimer(StaminaHandle, this, &AAmmoQuickCharacter::RecoveringStamina, StaminaRecoveryRate, true);
		bSprinting = false;
	}
}

void AAmmoQuickCharacter::RecoveringStamina()
{
	if (StaminaLeft < MaxStamina)
	{
		StaminaLeft += StaminaRecoveryMagnitude;
	}
	else
	{
		GetWorldTimerManager().ClearTimer(StaminaHandle);
	}
}

void AAmmoQuickCharacter::Dash()
{
	if (bCanDash && StaminaLeft >= DashStaminaUsage)
	{
		FVector Vel = GetVelocity();
		
		ACharacter::LaunchCharacter(FVector(Vel.X, Vel.Y, 0.f).GetSafeNormal() * DashDistance, true, false);
		
		bCanDash = false;
		StaminaLeft -= DashStaminaUsage;
		GetWorldTimerManager().SetTimer(DashHandle, this, &AAmmoQuickCharacter::ResetDash, DashCooldown, false);
	}
}

void AAmmoQuickCharacter::ResetDash()
{
	bCanDash = true;
	GetWorldTimerManager().SetTimer(StaminaHandle, this, &AAmmoQuickCharacter::RecoveringStamina, StaminaRecoveryRate, true);
}

void AAmmoQuickCharacter::Warp()
{
	if (bCanWarp && FuelLeft >= WarpFuelConsumption)
	{
		FVector CameraForward = FirstPersonCameraComponent->GetForwardVector();
		GetCharacterMovement()->BrakingFrictionFactor = 0.f;
		
		ACharacter::LaunchCharacter(FVector(CameraForward.X, CameraForward.Y, 0.f).GetSafeNormal() * WarpDistance, true, false);
		
		GetWorldTimerManager().SetTimer(WarpHandle, this, &AAmmoQuickCharacter::StopWarp, WarpStop, false);
		FuelLeft -= WarpFuelConsumption;
		
		if (FuelUseSound)
		{
			UGameplayStatics::PlaySoundAtLocation(GetWorld(), FuelUseSound, GetActorLocation());
		}
		
		bCanWarp = false;
	}
}

void AAmmoQuickCharacter::StopWarp()
{
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->BrakingFrictionFactor = 2.f;
	GetWorldTimerManager().SetTimer(WarpHandle, this, &AAmmoQuickCharacter::ResetWarp, WarpCooldown, false);
}

void AAmmoQuickCharacter::ResetWarp()
{
	bCanWarp = true;
}


bool AAmmoQuickCharacter::PickupAmmo(int32 Capacity)
{
	if (ammo + Capacity <= maxAmmo)
	{
		ammo += Capacity;
		if (clip < clipSize)
		{
			ReloadClip();
		}
		return true;
	}
	return false;
}

bool AAmmoQuickCharacter::PickupFuel(float Capacity)
{
	if (FuelLeft + Capacity <= MaxFuel)
	{
		FuelLeft += Capacity;
		return true;
	}
	return false;
}

void AAmmoQuickCharacter::AutoFire()
{
	GetWorldTimerManager().SetTimer(AutoFireHandle, this, &AAmmoQuickCharacter::OnFire, FireRate, true, 0.f);
}

void AAmmoQuickCharacter::StopAutoFire()
{
	GetWorldTimerManager().ClearTimer(AutoFireHandle);
}

void AAmmoQuickCharacter::OnFire()
{
	if (clip)
	{
		// try and fire a projectile
		if (ProjectileClass != NULL)
		{
			UWorld* const World = GetWorld();
			if (World != NULL)
			{
				if (bUsingMotionControllers)
				{
					const FRotator SpawnRotation = VR_MuzzleLocation->GetComponentRotation();
					const FVector SpawnLocation = VR_MuzzleLocation->GetComponentLocation();
					World->SpawnActor<AAmmoQuickProjectile>(ProjectileClass, SpawnLocation, SpawnRotation);
				}
				else
				{
					const FRotator SpawnRotation = GetControlRotation();
					// MuzzleOffset is in camera space, so transform it to world space before offsetting from the character location to find the final muzzle position
					const FVector SpawnLocation = ((FP_MuzzleLocation != nullptr) ? FP_MuzzleLocation->GetComponentLocation() : GetActorLocation()) + SpawnRotation.RotateVector(GunOffset);

					//Set Spawn Collision Handling Override
					FActorSpawnParameters ActorSpawnParams;
					ActorSpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;

					// spawn the projectile at the muzzle
					World->SpawnActor<AAmmoQuickProjectile>(ProjectileClass, SpawnLocation, SpawnRotation, ActorSpawnParams);
				}
				AddControllerPitchInput(recoilRate);
			}
		}

		// try and play the sound if specified
		if (FireSound != NULL)
		{
			UGameplayStatics::PlaySoundAtLocation(this, FireSound, GetActorLocation());
		}

		// try and play a firing animation if specified
		if (FireAnimation != NULL)
		{
			// Get the animation object for the arms mesh
			UAnimInstance* AnimInstance = Mesh1P->GetAnimInstance();
			if (AnimInstance != NULL)
			{
				AnimInstance->Montage_Play(FireAnimation, 1.f);
			}
		}

		--clip;
		if (!clip)
		{
			ReloadClip();
		}
	}
	else
	{
		ReloadClip();
	}
}

void AAmmoQuickCharacter::ReloadClip()
{
	if (clip < clipSize)
	{
		if (ammo + clip > clipSize)
		{
			ammo -= clipSize - clip;
			clip = clipSize;
		}
		else
		{
			clip += ammo;
			ammo = 0;
		}
		StopAutoFire();
		
		if (ReloadSound)
		{
			UGameplayStatics::PlaySoundAtLocation(GetWorld(), ReloadSound, GetActorLocation());
		}
	}
}

void AAmmoQuickCharacter::OnResetVR()
{
	UHeadMountedDisplayFunctionLibrary::ResetOrientationAndPosition();
}

void AAmmoQuickCharacter::BeginTouch(const ETouchIndex::Type FingerIndex, const FVector Location)
{
	if (TouchItem.bIsPressed == true)
	{
		return;
	}
	if ((FingerIndex == TouchItem.FingerIndex) && (TouchItem.bMoved == false))
	{
		OnFire();
	}
	TouchItem.bIsPressed = true;
	TouchItem.FingerIndex = FingerIndex;
	TouchItem.Location = Location;
	TouchItem.bMoved = false;
}

void AAmmoQuickCharacter::EndTouch(const ETouchIndex::Type FingerIndex, const FVector Location)
{
	if (TouchItem.bIsPressed == false)
	{
		return;
	}
	TouchItem.bIsPressed = false;
}

//Commenting this section out to be consistent with FPS BP template.
//This allows the user to turn without using the right virtual joystick

//void AAmmoQuickCharacter::TouchUpdate(const ETouchIndex::Type FingerIndex, const FVector Location)
//{
//	if ((TouchItem.bIsPressed == true) && (TouchItem.FingerIndex == FingerIndex))
//	{
//		if (TouchItem.bIsPressed)
//		{
//			if (GetWorld() != nullptr)
//			{
//				UGameViewportClient* ViewportClient = GetWorld()->GetGameViewport();
//				if (ViewportClient != nullptr)
//				{
//					FVector MoveDelta = Location - TouchItem.Location;
//					FVector2D ScreenSize;
//					ViewportClient->GetViewportSize(ScreenSize);
//					FVector2D ScaledDelta = FVector2D(MoveDelta.X, MoveDelta.Y) / ScreenSize;
//					if (FMath::Abs(ScaledDelta.X) >= 4.0 / ScreenSize.X)
//					{
//						TouchItem.bMoved = true;
//						float Value = ScaledDelta.X * BaseTurnRate;
//						AddControllerYawInput(Value);
//					}
//					if (FMath::Abs(ScaledDelta.Y) >= 4.0 / ScreenSize.Y)
//					{
//						TouchItem.bMoved = true;
//						float Value = ScaledDelta.Y * BaseTurnRate;
//						AddControllerPitchInput(Value);
//					}
//					TouchItem.Location = Location;
//				}
//				TouchItem.Location = Location;
//			}
//		}
//	}
//}

void AAmmoQuickCharacter::MoveForward(float Value)
{
	if (Value != 0.0f)
	{
		// add movement in that direction
		AddMovementInput(GetActorForwardVector(), Value);
	}
	else
	{
		Walk();
	}
}

void AAmmoQuickCharacter::MoveRight(float Value)
{
	if (Value != 0.0f)
	{
		// add movement in that direction
		AddMovementInput(GetActorRightVector(), Value);
		Walk();
	}
}

void AAmmoQuickCharacter::TurnAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerYawInput(Rate * BaseTurnRate * GetWorld()->GetDeltaSeconds());
}

void AAmmoQuickCharacter::LookUpAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerPitchInput(Rate * BaseLookUpRate * GetWorld()->GetDeltaSeconds());
}

bool AAmmoQuickCharacter::EnableTouchscreenMovement(class UInputComponent* PlayerInputComponent)
{
	if (FPlatformMisc::SupportsTouchInput() || GetDefault<UInputSettings>()->bUseMouseForTouch)
	{
		PlayerInputComponent->BindTouch(EInputEvent::IE_Pressed, this, &AAmmoQuickCharacter::BeginTouch);
		PlayerInputComponent->BindTouch(EInputEvent::IE_Released, this, &AAmmoQuickCharacter::EndTouch);

		//Commenting this out to be more consistent with FPS BP template.
		//PlayerInputComponent->BindTouch(EInputEvent::IE_Repeat, this, &AAmmoQuickCharacter::TouchUpdate);
		return true;
	}
	
	return false;
}

FString AAmmoQuickCharacter::GetAmmoString()
{
	return FString::FromInt(clip) + FString(TEXT(" | ")) + FString::FromInt(ammo);
}

float AAmmoQuickCharacter::GetStaminaProgress()
{
	return Stamina / MaxStamina; 
}

float AAmmoQuickCharacter::GetFuelProgress()
{
	return Fuel / MaxFuel;
}

void AAmmoQuickCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (StaminaLeft != Stamina)
	{
		float StaminaChange = FMath::Abs(StaminaLeft - Stamina) * DeltaTime;
		Stamina = FMath::Lerp(Stamina, StaminaLeft, StaminaChange);
	}

	if (FuelLeft != Fuel)
	{
		float FuelChange = FMath::Abs(FuelLeft - Fuel) * DeltaTime;
		Fuel = FMath::Lerp(Fuel, FuelLeft, FuelChange);
	}
}