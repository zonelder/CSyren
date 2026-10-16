// Ported from E:/src/lib/cstdmf/job_system2.hpp.
// CSyren changes: engine logging/asserts, namespace, valid default thread count,
// pinned jobs, and explicit worker shutdown. The enkiTS scheduler is unchanged.
#ifndef CSYREN_JOB_SYSTEM2_HPP
#define CSYREN_JOB_SYSTEM2_HPP

#include "log.h"
#include "assert_helpler.h"
#include <algorithm>
#include "third_party/enkiTS/src/TaskScheduler.h"

#include <vector>
#include <cstdint>
#include <cassert>
#include <deque>
#include <memory>
#include <type_traits>
#include <utility>

// Temporary investigation of incomplete enki tasks. Set to 0 to remove all diagnostic work.
#if defined(_DEBUG)
#define CSTDMF_JOB_SYSTEM_DIAGNOSTICS 1
#else
#define CSTDMF_JOB_SYSTEM_DIAGNOSTICS 0
#endif
#if CSTDMF_JOB_SYSTEM_DIAGNOSTICS
#include <sstream>
#include <string>
#include <typeinfo>
#endif

namespace csyren::cstdmf::multicore
{
	class IJob
	{
	public:
		virtual ~IJob() = default;
		virtual void execute( uint32_t start, uint32_t end, uint32_t threadNum ) = 0;
	};

	template < class Fn >
		requires std::is_invocable_v< Fn, uint32_t, uint32_t, uint32_t >
	class LambdaJob : public IJob
	{
	public:
		LambdaJob( Fn&& f ) noexcept : fn_( std::move( f ) )
		{
		}

		void execute( uint32_t start, uint32_t end, uint32_t threadNum ) override
		{
			fn_( start, end, threadNum );
		}

	private:
		Fn fn_;
	};

	struct JobHandle
	{
		uint32_t id = 0xFFFFFFFF;  // local index in thread's entries
		uint16_t thread = 0xFFFF;  // thread that scheduled it
		[[nodiscard]] bool isValid() const noexcept
		{
			return id != 0xFFFFFFFF && thread != 0xFFFF;
		}
	};

	class JobSystem
	{
	public:
		static JobSystem& instance()
		{
			static JobSystem m;
			return m;
		}

		void init( uint32_t numThreads = 0 )
		{
			assert( numThreads == 0 || numThreads >= 1 );
			log::info("init JobSystem with {} threads", numThreads);
			_scheduler.Initialize( numThreads ? numThreads : std::max(1u, std::thread::hardware_concurrency()) );
			_numThreads = _scheduler.GetNumTaskThreads();
			_entries.resize( _numThreads );
		}

		void fini()
		{
            // Callers must first wait for their explicit terminal task.
			clear();
            _scheduler.ShutdownNow();
            _entries.clear();
            _numThreads = 0;
		}

		[[nodiscard]] JobHandle schedule( IJob* job, uint32_t setSize = 1, uint32_t minRange = 1 )
		{
			CS_ASSERT( job != nullptr );
			auto threadIdx = _scheduler.GetThreadNum();
			CS_ASSERT( threadIdx < _numThreads );

			auto& localEntries = _entries[ threadIdx ];

			JobEntry entry;
			entry.job = job;
			#if CSTDMF_JOB_SYSTEM_DIAGNOSTICS
			entry.debugType = &typeid( *job );
			#endif
			entry.wrapper = std::make_unique< EnkiAdapter >( job, setSize, minRange );

			uint32_t id = static_cast< uint32_t >( localEntries.size() );
			localEntries.emplace_back( std::move( entry ) );

			return { id, static_cast< uint16_t >( threadIdx ) };
		}

        // Added by the CSyren port: main-thread jobs participate in the same DAG.
        [[nodiscard]] JobHandle schedulePinned(IJob* job, uint32_t targetThread = 0)
        {
            CS_ASSERT(job != nullptr && targetThread < _numThreads);
            const auto thread = _scheduler.GetThreadNum();
            CS_ASSERT(thread < _numThreads);
            JobEntry entry;
            entry.job = job;
            entry.pinned = true;
#if CSTDMF_JOB_SYSTEM_DIAGNOSTICS
            entry.debugType = &typeid(*job);
#endif
            entry.wrapper = std::make_unique<PinnedAdapter>(job, targetThread);
            auto& entries = _entries[thread];
            const auto index = static_cast<uint32_t>(entries.size());
            entries.emplace_back(std::move(entry));
            return {index, static_cast<uint16_t>(thread)};
        }

		void addDependency( JobHandle child, JobHandle parent )
		{
			CS_ASSERT( child.isValid() && parent.isValid() );
			CS_ASSERT( child.thread < _numThreads && parent.thread < _numThreads );
			CS_ASSERT( child.id < _entries[ child.thread ].size() );
			CS_ASSERT( parent.id < _entries[ parent.thread ].size() );

			enki::Dependency& dep = _entries[ child.thread ][ child.id ].deps.emplace_back();
			_entries[ child.thread ][ child.id ].wrapper->SetDependency( dep,
																		 _entries[ parent.thread ][ parent.id ].wrapper.get() );
			
		}

		void submitRoot( JobHandle handle )
		{
			if ( !handle.isValid() || handle.thread >= _numThreads || handle.id >= _entries[ handle.thread ].size() )
				return;

			CS_ASSERT( _entries[ handle.thread ][ handle.id ].deps.empty() && "submitRoot only for root jobs" );
			#if CSTDMF_JOB_SYSTEM_DIAGNOSTICS
			_entries[ handle.thread ][ handle.id ].submittedAsRoot = true;
			#endif
			auto& entry = _entries[handle.thread][handle.id];
            if (entry.pinned)
                _scheduler.AddPinnedTask(static_cast<enki::IPinnedTask*>(entry.wrapper.get()));
            else
                _scheduler.AddTaskSetToPipe(static_cast<enki::ITaskSet*>(entry.wrapper.get()));
		}

		/// Runs other jobs on this thread until \a handle is done, with this thread's number: a job that waits
		/// must hold nothing per-thread across the wait (a thread_local scratch in use, the thread's
		/// RenderServices mid-change) - the jobs it runs meanwhile use the same ones.
		void wait( JobHandle handle )
		{
			if ( !handle.isValid() || handle.thread >= _numThreads || handle.id >= _entries[ handle.thread ].size() )
				return;
			_scheduler.WaitforTask( _entries[ handle.thread ][ handle.id ].wrapper.get() );
		}

		// WARNING: Do not use enki::TaskScheduler::WaitforAll() to establish that
		// jobs are safe to destroy. We reproduced an early return in the checked
		// upstream implementation: its idle-state scan can observe worker A asleep,
		// then worker B completes a predecessor and A starts its dependent job.
		// B goes idle and the queues are empty, so the scan returns while A is busy.
		// This exact race has not been confirmed by the upstream maintainer.
		//
		// The author recommends an explicit completion point depending on the jobs
		// to wait for, followed by WaitforTask on that point. All dependencies must
		// be linked before submitting roots. We use an ordinary empty finisher job:
		// wait(finisherHandle), then clear(). Nested work must be covered as well.
		// https://github.com/dougbinks/enkiTS/issues/86#issuecomment-1487335696
		// https://github.com/dougbinks/enkiTS/issues/86#issuecomment-1487526648
		// clear() only releases completed jobs; it does not wait.
		void clear()
		{
			#if CSTDMF_JOB_SYSTEM_DIAGNOSTICS
			if ( !debugCheckAllComplete() )
			{
				CS_ASSERT( false && "JobSystem found incomplete jobs before cleanup; see diagnostic above" );
				return;
			}
			#endif
			for ( auto& local : _entries )
				local.clear();
		}

		[[nodiscard]] uint32_t getNumThreads() const noexcept
		{
			return _numThreads;
		}

		[[nodiscard]] uint32_t currentThreadIndex() const noexcept
		{
			return _scheduler.GetThreadNum();
		}

		#if CSTDMF_JOB_SYSTEM_DIAGNOSTICS
		// Inspect this in the debugger if the clear assertion fires; the log may truncate a large graph.
		[[nodiscard]] const std::string& lastClearDiagnostic() const noexcept
		{
			return _lastClearDiagnostic;
		}
		#endif

	private:
		// Internal implementation.
		class EnkiAdapter : public enki::ITaskSet
		{
		public:
			EnkiAdapter( IJob* job, uint32_t setSize, uint32_t minRange ) : enki::ITaskSet( setSize, minRange ), _job( job )
			{
			}

			void ExecuteRange( enki::TaskSetPartition range, uint32_t threadNum ) override
			{
				_job->execute( range.start, range.end, threadNum );
			}

		private:
			IJob* _job;
		};

        class PinnedAdapter final : public enki::IPinnedTask
        {
        public:
            PinnedAdapter(IJob* job, uint32_t target) : enki::IPinnedTask(target), _job(job) {}
            void Execute() override { _job->execute(0, 1, threadNum); }
        private:
            IJob* _job;
        };

		struct JobEntry
		{
			IJob* job;
			std::unique_ptr< enki::ICompletable > wrapper;
            bool pinned = false;
			std::deque< enki::Dependency > deps;
			#if CSTDMF_JOB_SYSTEM_DIAGNOSTICS
			const std::type_info* debugType = nullptr;
			bool submittedAsRoot = false;
			#endif
		};
		using JobEntries = std::vector< JobEntry >;

		#if CSTDMF_JOB_SYSTEM_DIAGNOSTICS
		bool debugCheckAllComplete()
		{
			bool complete = true;
			for ( const auto& entries : _entries )
			{
				for ( const auto& entry : entries )
				{
					if ( !entry.wrapper->GetIsComplete() )
					{
						complete = false;
						break;
					}
				}
				if ( !complete )
					break;
			}
			if ( complete )
				return true;

			const auto describe = [ this ]( const enki::ICompletable* task )
			{
				std::ostringstream description;
				for ( size_t thread = 0; thread < _entries.size(); ++thread )
				{
					const auto& entries = _entries[ thread ];
					for ( size_t id = 0; id < entries.size(); ++id )
					{
						const auto& entry = entries[ id ];
						if ( entry.wrapper.get() == task )
						{
							description << '[' << thread << ':' << id << "] type=" << entry.debugType->name()
								<< " complete=" << entry.wrapper->GetIsComplete()
								<< " root=" << entry.submittedAsRoot;
							return description.str();
						}
					}
				}
				description << "external task=" << task;
				return description.str();
			};

			std::ostringstream report;
			for ( size_t thread = 0; thread < _entries.size(); ++thread )
			{
				auto& entries = _entries[ thread ];
				for ( size_t id = 0; id < entries.size(); ++id )
				{
					auto& entry = entries[ id ];
					if ( entry.wrapper->GetIsComplete() )
						continue;
					report << "  pending " << describe( entry.wrapper.get() ) << " job=" << entry.job << '\n';
					for ( auto& dependency : entry.deps )
					{
						report << "    waits for " << describe( dependency.GetDependencyTask() ) << '\n';
					}
					for ( size_t childThread = 0; childThread < _entries.size(); ++childThread )
					{
						auto& children = _entries[ childThread ];
						for ( size_t childId = 0; childId < children.size(); ++childId )
						{
							for ( auto& dependency : children[ childId ].deps )
							{
								if ( dependency.GetDependencyTask() == entry.wrapper.get() )
									report << "    awaited by " << describe( children[ childId ].wrapper.get() ) << '\n';
							}
						}
					}
				}
			}
			_lastClearDiagnostic = report.str();
			log::error("JobSystem: incomplete enki tasks before cleanup:\n{}", _lastClearDiagnostic);
			return false;
		}
		#endif

		enki::TaskScheduler _scheduler;
		std::vector< JobEntries > _entries;//[thread][local_id]
		uint32_t _numThreads = 0;
		#if CSTDMF_JOB_SYSTEM_DIAGNOSTICS
		std::string _lastClearDiagnostic;
		#endif
	};
}  // namespace csyren::cstdmf::multicore

#undef CSTDMF_JOB_SYSTEM_DIAGNOSTICS
#endif
