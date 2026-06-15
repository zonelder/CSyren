#pragma once

#include <vector>
#include <memory>

#include "cstdmf/assert_helpler.h"


#define CS_STATIC(Class) Class : public ::csyren::core::details::Singleton<Class>

namespace csyren::core::details
{
	class SingletonRegistry;
	class ISingleton
	{
		friend SingletonRegistry;
	public:
		virtual ~ISingleton() = default;
	protected:
		virtual void init() {}
		virtual void shutdown() {}
	};

	template<class T>
	class Singleton : public ISingleton
	{
		friend SingletonRegistry;
	public:
		static T& instance()
		{
			return *instance_;
		}

		static T* instancePtr()
		{
			return instance_;
		}

	private:
		inline static T* instance_ = nullptr;
	};

	class SingletonRegistry
	{
	public:
		static void initializeAll()
		{
			for (auto& s : singletons_)
			{
				s->init();
			}
		}

		static void shutdownAll()
		{
			// shutdown в обратном порядке
			for (auto it = singletons_.rbegin(); it != singletons_.rend(); ++it)
			{
				(*it)->shutdown();
			}
		}

		template<class T, class... Args>
		static T& add(Args&&... args)
		{
			static_assert(std::is_base_of_v<ISingleton, T>);
			CS_ASSERT(Singleton<T>::instance_ == nullptr);

			auto ptr = std::make_unique<T>(std::forward<Args>(args)...);

			T& ref = *ptr;

			Singleton<T>::instance_ = ptr.get();
			singletons_.push_back(std::move(ptr));

			return ref;
		}

	private:
		inline static std::vector<std::unique_ptr<ISingleton>> singletons_;
	};

}

