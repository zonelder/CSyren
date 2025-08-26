static_assert(false, "its a meta file.");


/*
TODO:

//------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
1. Scene Serialization & Hot Reloading
	Goal: To create, edit, and view scenes by changing a text file, without restarting the application. This is the biggest workflow accelerator.
	Implementation:
	SceneSerializer Class:
	Create a class SceneSerializer(Scene& scene, ResourceManager& rm).
	Use a library like nlohmann/json to parse .json files.
	load(path) method: Reads the JSON, iterates through an "entities" array. For each entity, it calls scene.createEntity() and then iterates through its "components" object, calling scene.addComponent<T>() and filling its data from the JSON.
	save(path) method: Iterates through the scene's entities using a view (scene.view<Transform>()) and writes their data back to a JSON file.
	Hot Reloading Logic (in Application::run):
	Store the FILETIME of the loaded scene file.
	In the main loop, once per second, check the file's last write time using GetFileTime().
	If the timestamp has changed:
	Call scene.clear() to destroy all current entities.
	Call serializer.load(path) to rebuild the scene from the modified file.
	Update the stored FILETIME.
//------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
2. Sprite Rendering & Layer Sorting
	Goal: To render 2D images (sprites) in the correct order (e.g., UI on top of characters, characters on top of background).
	Implementation:
	SpriteRenderer Component:
	Add fields: TextureHandle texture, Color color, int layer, int orderInLayer.
	SpriteRenderSystem Class:
	It queries for entities with Transform and SpriteRenderer.
	Render Key: For each entity, generate a 64-bit sort key: uint64_t key = ((uint64_t)layer << 32) | orderInLayer;.
	Gather Phase: Create a std::vector of (sort_key, entity_id) pairs for all visible sprites.
	Sort Phase: std::sort() this vector.
	Dispatch Phase: Iterate through the sorted vector. Use a single, static quad mesh. For each sprite, update a PerObject constant buffer with its world matrix (derived from Transform's position, rotation, scale) and color. Bind its texture. Call DrawInstanced().
	Shader: Create a simple Sprite.hlsl shader. The VS applies the world matrix to the quad vertices. The PS samples the texture and multiplies by the color.
	Serialization: Add SpriteRenderer to the SceneSerializer.
	3. 2D Physics & Collision Detection
	Goal: To make objects interact with each other. Characters should stand on platforms, and triggers should activate events.
Implementation:
	Collider Components:
	BoxCollider2D: Vector2 size, Vector2 offset.
	CircleCollider2D: float radius, Vector2 offset.
	RigidBody2D Component:
	enum BodyType { Static, Kinematic, Dynamic }.
	Vector2 velocity, float gravityScale.
	PhysicsSystem Class (runs in FixedUpdate):
	Movement: For all Dynamic bodies, apply gravity (velocity.y += GRAVITY * gravityScale * fixedDeltaTime;) and update position (transform.position += velocity * fixedDeltaTime;).
	Collision Detection: Use a nested loop (for each body A, for each body B) to check for intersections (AABB vs AABB tests).
	Collision Resolution: If a collision is detected, resolve it. For a simple platformer, a basic resolution is to move the dynamic body out of the static body along the axis of least penetration.
	Events: When a collision occurs, publish an event: bus.publish(CollisionEvent{entityA, entityB});.
	Serialization: Add BoxCollider2D and RigidBody2D to the SceneSerializer.

//------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
4. Sprite Animation
	Goal: To play frame-by-frame animations on sprites to make the world feel alive.
	Implementation:
	Animation Resource:
	A class that holds a std::vector<TextureHandle> frames and a float frameRate. Load this from a file.
	SpriteSheet Resource: A texture atlas. Your ResourceManager needs a method to "slice" a SpriteSheet into multiple TextureHandles based on grid dimensions.
	Animator Component:
	AnimationHandle currentAnimation.
	Internal state: float timer, int currentFrameIndex.
	AnimationSystem Class (runs in Update):
	For each entity with an Animator and SpriteRenderer:
	Increment timer += deltaTime.
	If timer >= (1.0f / animation.frameRate):
	Increment currentFrameIndex.
	Reset timer: timer = 0.
	Update the sprite: spriteRenderer.texture = animation.frames[currentFrameIndex].
	Serialization: Add the Animator component to the SceneSerializer, referencing animation resources by name.

//------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
5. 2D Camera Controller
	Goal: To have the camera follow the player and stay within the level's boundaries.
	Implementation:
	Camera Component: Ensure it's configured for ProjectionType::Orthographic.
	Player Tag Component: An empty component to identify the player entity.
	CameraFollowSystem Class (runs in LateUpdate, after player movement):
	Find the entity with the Player tag.
	Find the entity with the Camera component.
	In the update loop, smoothly move the camera's position towards the player's position using a Lerp function: camera.position = lerp(camera.position, player.position, followSpeed * deltaTime);.
	Bounds: Add an optional CameraBounds component (a Rect) to a level entity. After calculating the new camera position, clamp it so it doesn't go outside the bounds.
	Serialization: Add the Camera component and its properties (FOV/ortho size, background color) to the SceneSerializer.

*/





