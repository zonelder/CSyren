#pragma once


namespace csyren::components::demo::space_shooter
{
	struct MovableComponent
	{
		float speed;
		MovableComponent(float s = 100.0f) : speed(s) {}
	};

	struct DamageableComponent
	{
		float maxHealth;
		float currentHealth;
		DamageableComponent(float mh = 100.0f) : maxHealth(mh), currentHealth(mh) {}
	};

	struct FireableComponent
	{
		float fireRate;
		float lastFireTime;
		FireableComponent(float fr = 1.0f) : fireRate(fr), lastFireTime(0.0f) {}
	};

	//marker component
	struct PlayerComponent{};
	
	//marker component
	struct EmenyComponent{};


	struct VelocityComponent
	{
		float x, y;
	};

	struct CircleColliderComponent
	{
		float radius;
		CircleColliderComponent(float r = 1.0f) : radius(r) {}
	};


	struct DamageComponent
	{
		float damage;
	};

	struct ProjectileComponent
	{
		float damage;
		float lifetime;
		float currentLifetime;

		ProjectileComponent(float d = 1.0f, float lt = 5.0f) : damage(d), lifetime(lt), currentLifetime(0.0f) {}
	};

}