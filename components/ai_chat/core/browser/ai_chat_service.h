// Copyright (c) 2024 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef BRAVE_COMPONENTS_AI_CHAT_CORE_BROWSER_AI_CHAT_SERVICE_H_
#define BRAVE_COMPONENTS_AI_CHAT_CORE_BROWSER_AI_CHAT_SERVICE_H_

#include <stddef.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_multi_source_observation.h"
#include "base/threading/sequence_bound.h"
#include "brave/components/ai_chat/core/browser/ai_chat_credential_manager.h"
#include "brave/components/ai_chat/core/browser/ai_chat_database.h"
#include "brave/components/ai_chat/core/browser/ai_chat_feedback_api.h"
#include "brave/components/ai_chat/core/browser/ai_chat_metrics.h"
#include "brave/components/ai_chat/core/browser/associated_content_delegate.h"
#include "brave/components/ai_chat/core/browser/conversation_handler.h"
#include "brave/components/ai_chat/core/browser/engine/engine_consumer.h"
#include "brave/components/ai_chat/core/browser/tools/tool_provider_factory.h"
#include "brave/components/ai_chat/core/common/mojom/ai_chat.mojom-forward.h"
#include "brave/components/ai_chat/core/common/mojom/common.mojom-forward.h"
#include "brave/components/ai_chat/core/common/mojom/tab_tracker.mojom.h"
#include "brave/components/skus/common/skus_sdk.mojom.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/prefs/pref_change_registrar.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "mojo/public/cpp/bindings/remote_set.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

namespace os_crypt_async {
class Encryptor;
class OSCryptAsync;
}  // namespace os_crypt_async

class PrefService;
namespace network {
class SharedURLLoaderFactory;
}  // namespace network

namespace ai_chat {

class ModelService;
class TabTrackerService;
class AIChatMetrics;
class MemoryStorageTool;

/**
 * @brief Main entry point for creating and consuming AI Chat conversations.
 *
 * This service manages the lifecycle of AI Chat conversations, handles
 * persistence, and coordinates between the UI, the model service, and other
 * components.
 */
class AIChatService : public KeyedService,
                      public mojom::Service,
                      public ConversationHandler::Observer,
                      public mojom::TabDataObserver {
 public:
  using SkusServiceGetter =
      base::RepeatingCallback<mojo::PendingRemote<skus::mojom::SkusService>()>;
  using GetSuggestedTopicsCallback = base::OnceCallback<void(
      base::expected<std::vector<std::string>, mojom::APIError>)>;
  using GetFocusTabsCallback = base::OnceCallback<void(
      base::expected<std::vector<std::string>, mojom::APIError>)>;

  /**
   * @brief Constructs an AIChatService instance.
   *
   * @param model_service The service for managing AI models.
   * @param tab_tracker_service The service for tracking tab data.
   * @param ai_chat_credential_manager Manager for AI chat credentials.
   * @param profile_prefs The profile preferences service.
   * @param ai_chat_metrics Metrics recorder for AI chat.
   * @param os_crypt_async Async OS encryption service.
   * @param url_loader_factory Factory for URL loaders.
   * @param channel_string The release channel string.
   * @param profile_path The path to the user profile directory.
   * @param tool_provider_factories Factories for creating tool providers.
   */
  AIChatService(
      ModelService* model_service,
      TabTrackerService* tab_tracker_service,
      std::unique_ptr<AIChatCredentialManager> ai_chat_credential_manager,
      PrefService* profile_prefs,
      AIChatMetrics* ai_chat_metrics,
      os_crypt_async::OSCryptAsync* os_crypt_async,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      std::string_view channel_string,
      base::FilePath profile_path,
      // Factories of ToolProviders from other layers
      std::vector<std::unique_ptr<ToolProviderFactory>>
          tool_provider_factories = {});

  ~AIChatService() override;
  AIChatService(const AIChatService&) = delete;
  AIChatService& operator=(const AIChatService&) = delete;

  /**
   * @brief Creates a pending remote for the AI Chat Service.
   *
   * @return A pending remote for `mojom::Service`.
   */
  mojo::PendingRemote<mojom::Service> MakeRemote();

  /**
   * @brief Binds a pending receiver to this service.
   *
   * @param receiver The receiver to bind.
   */
  void Bind(mojo::PendingReceiver<mojom::Service> receiver);

  // KeyedService
  /**
   * @brief Shuts down the service.
   *
   * Cleans up resources, disconnects remotes, and invalidates weak pointers.
   */
  void Shutdown() override;

  // ConversationHandler::Observer
  /**
   * @brief Called when a request in progress status changes for a conversation handler.
   *
   * @param handler The conversation handler.
   * @param in_progress True if a request is in progress, false otherwise.
   */
  void OnRequestInProgressChanged(ConversationHandler* handler,
                                  bool in_progress) override;

  /**
   * @brief Called when a conversation entry is added.
   *
   * @param handler The conversation handler.
   * @param entry The added conversation turn.
   * @param maybe_associated_content Optional associated page contents.
   */
  void OnConversationEntryAdded(
      ConversationHandler* handler,
      mojom::ConversationTurnPtr& entry,
      std::optional<PageContents> maybe_associated_content) override;

  /**
   * @brief Called when a conversation entry is removed.
   *
   * @param handler The conversation handler.
   * @param entry_uuid The UUID of the removed entry.
   */
  void OnConversationEntryRemoved(ConversationHandler* handler,
                                  std::string entry_uuid) override;

  /**
   * @brief Called when a tool use event output is received.
   *
   * @param handler The conversation handler.
   * @param entry_uuid The UUID of the entry.
   * @param event_order The order of the event.
   * @param tool_use The tool use event data.
   */
  void OnToolUseEventOutput(ConversationHandler* handler,
                            std::string_view entry_uuid,
                            size_t event_order,
                            mojom::ToolUseEventPtr tool_use) override;

  /**
   * @brief Called when a client connection changes.
   *
   * @param handler The conversation handler.
   */
  void OnClientConnectionChanged(ConversationHandler* handler) override;

  /**
   * @brief Called when a conversation title changes.
   *
   * @param conversation_uuid The UUID of the conversation.
   * @param title The new title.
   */
  void OnConversationTitleChanged(const std::string& conversation_uuid,
                                  const std::string& title) override;

  /**
   * @brief Called when conversation token info changes.
   *
   * @param conversation_uuid The UUID of the conversation.
   * @param total_tokens Total tokens used.
   * @param trimmed_tokens Trimmed tokens count.
   */
  void OnConversationTokenInfoChanged(const std::string& conversation_uuid,
                                      uint64_t total_tokens,
                                      uint64_t trimmed_tokens) override;

  /**
   * @brief Called when associated content is updated.
   *
   * @param handler The conversation handler.
   */
  void OnAssociatedContentUpdated(ConversationHandler* handler) override;

  // mojom::TabDataObserver
  /**
   * @brief Called when tab data changes.
   *
   * @param tab_data A vector of tab data pointers.
   */
  void TabDataChanged(std::vector<mojom::TabDataPtr> tab_data) override;

  /**
   * @brief Adds a new conversation and returns the handler.
   *
   * @return A pointer to the created `ConversationHandler`.
   */
  ConversationHandler* CreateConversation();

  /**
   * @brief Provides memory tool for testing.
   *
   * @return A pointer to the `MemoryStorageTool`.
   */
  MemoryStorageTool* GetMemoryToolForTesting();

  /**
   * @brief Gets a conversation handler by UUID.
   *
   * @param uuid The UUID of the conversation.
   * @return A pointer to the `ConversationHandler`, or nullptr if not found.
   */
  ConversationHandler* GetConversation(std::string_view uuid);

  /**
   * @brief Asynchronously gets a conversation handler by UUID.
   *
   * @param conversation_uuid The UUID of the conversation.
   * @param callback Callback to run with the retrieved `ConversationHandler`.
   */
  void GetConversation(std::string_view conversation_uuid,
                       base::OnceCallback<void(ConversationHandler*)>);

  /**
   * @brief Creates and owns a ConversationHandler if one hasn't been made for the
   * associated_content_id yet.
   *
   * |associated_content_id| should not be stored. It is an ephemeral identifier for active browser content.
   *
   * @param associated_content_id The ID of the associated content.
   * @param associated_content Weak pointer to the associated content delegate.
   * @return A pointer to the `ConversationHandler`.
   */
  ConversationHandler* GetOrCreateConversationHandlerForContent(
      int associated_content_id,
      base::WeakPtr<AssociatedContentDelegate> associated_content);

  /**
   * @brief Creates and owns a new ConversationHandler associated with the provided content ID.
   *
   * |associated_content_id| should not be stored. It is an ephemeral identifier for active browser content.
   *
   * @param associated_content_id The ID of the associated content.
   * @param associated_content Weak pointer to the associated content delegate.
   * @return A pointer to the created `ConversationHandler`.
   */
  ConversationHandler* CreateConversationHandlerForContent(
      int associated_content_id,
      base::WeakPtr<AssociatedContentDelegate> associated_content);

  /**
   * @brief Removes all in-memory and persisted data for all conversations.
   *
   * @param begin_time Optional start time for deletion range.
   * @param end_time Optional end time for deletion range.
   */
  void DeleteConversations(std::optional<base::Time> begin_time = std::nullopt,
                           std::optional<base::Time> end_time = std::nullopt);

  /**
   * @brief Remove only web-content data from conversations.
   *
   * @param begin_time Optional start time for deletion range.
   * @param end_time Optional end time for deletion range.
   * @param callback Callback to run after deletion.
   */
  void DeleteAssociatedWebContent(
      std::optional<base::Time> begin_time = std::nullopt,
      std::optional<base::Time> end_time = std::nullopt,
      base::OnceCallback<void(bool)> callback = base::DoNothing());

  /**
   * @brief Opens a conversation with staged entries.
   *
   * @param associated_content Weak pointer to associated content delegate.
   * @param open_ai_chat Closure to open the AI chat UI.
   */
  void OpenConversationWithStagedEntries(
      base::WeakPtr<AssociatedContentDelegate> associated_content,
      base::OnceClosure open_ai_chat);

  /**
   * @brief Maybe associates content with a conversation.
   *
   * @param content Pointer to associated content delegate.
   * @param conversation_uuid The UUID of the conversation.
   */
  void MaybeAssociateContent(AssociatedContentDelegate* content,
                             const std::string& conversation_uuid);

  /**
   * @brief Associates owned content with a conversation.
   *
   * @param content Unique pointer to associated content delegate.
   * @param conversation_uuid The UUID of the conversation.
   */
  void AssociateOwnedContent(std::unique_ptr<AssociatedContentDelegate> content,
                             const std::string& conversation_uuid);

  /**
   * @brief Disassociates content from a conversation.
   *
   * @param content The associated content to remove.
   * @param conversation_uuid The UUID of the conversation.
   */
  void DisassociateContent(const mojom::AssociatedContentPtr& content,
                           const std::string& conversation_uuid);

  /**
   * @brief Gets focused tabs for a topic.
   *
   * @param tabs Vector of tabs to search.
   * @param topic The topic to focus on.
   * @param callback Callback to return the result.
   */
  void GetFocusTabs(const std::vector<Tab>& tabs,
                    const std::string& topic,
                    GetFocusTabsCallback callback);

  /**
   * @brief Gets suggested topics from tabs.
   *
   * @param tabs Vector of tabs to analyze.
   * @param callback Callback to return suggested topics.
   */
  void GetSuggestedTopics(const std::vector<Tab>& tabs,
                          GetSuggestedTopicsCallback callback);

  // mojom::Service
  /**
   * @brief Marks the user agreement as accepted.
   */
  void MarkAgreementAccepted() override;

  /**
   * @brief Enables the storage preference.
   */
  void EnableStoragePref() override;

  /**
   * @brief Dismisses the storage notice.
   */
  void DismissStorageNotice() override;

  /**
   * @brief Dismisses the premium prompt.
   */
  void DismissPremiumPrompt() override;

  /**
   * @brief Gets the list of skills.
   *
   * @param callback Callback to return the skills.
   */
  void GetSkills(GetSkillsCallback callback) override;

  /**
   * @brief Creates a new skill.
   *
   * @param shortcut The skill shortcut.
   * @param prompt The skill prompt.
   * @param model The model to use for the skill.
   */
  void CreateSkill(const std::string& shortcut,
                   const std::string& prompt,
                   const std::optional<std::string>& model) override;

  /**
   * @brief Updates an existing skill.
   *
   * @param id The skill ID.
   * @param shortcut The new shortcut.
   * @param prompt The new prompt.
   * @param model The new model.
   */
  void UpdateSkill(const std::string& id,
                   const std::string& shortcut,
                   const std::string& prompt,
                   const std::optional<std::string>& model) override;

  /**
   * @brief Deletes a skill.
   *
   * @param id The ID of the skill to delete.
   */
  void DeleteSkill(const std::string& id) override;

  /**
   * @brief Gets the list of conversations.
   *
   * @param callback Callback to return the conversations.
   */
  void GetConversations(GetConversationsCallback callback) override;

  /**
   * @brief Gets the action menu list.
   *
   * @param callback Callback to return the action menu list.
   */
  void GetActionMenuList(GetActionMenuListCallback callback) override;

  /**
   * @brief Gets the premium status.
   *
   * @param callback Callback to return the premium status.
   */
  void GetPremiumStatus(GetPremiumStatusCallback callback) override;

  /**
   * @brief Deletes a conversation by ID.
   *
   * @param id The ID of the conversation to delete.
   */
  void DeleteConversation(const std::string& id) override;

  /**
   * @brief Renames a conversation.
   *
   * @param id The ID of the conversation.
   * @param new_name The new name for the conversation.
   */
  void RenameConversation(const std::string& id,
                          const std::string& new_name) override;

  /**
   * @brief Checks if a conversation exists.
   *
   * @param conversation_uuid The UUID of the conversation.
   * @param callback Callback to return true if exists, false otherwise.
   */
  void ConversationExists(const std::string& conversation_uuid,
                          ConversationExistsCallback callback) override;

  /**
   * @brief Binds a conversation handler to a remote UI.
   *
   * @param uuid The UUID of the conversation.
   * @param receiver The pending receiver for the handler.
   * @param conversation_ui_handler The remote UI handler.
   */
  void BindConversation(
      const std::string& uuid,
      mojo::PendingReceiver<mojom::ConversationHandler> receiver,
      mojo::PendingRemote<mojom::ConversationUI> conversation_ui_handler)
      override;

  /**
   * @brief Binds metrics receiver.
   *
   * @param metrics The pending receiver for metrics.
   */
  void BindMetrics(mojo::PendingReceiver<mojom::Metrics> metrics) override;

  /**
   * @brief Binds a service observer.
   *
   * @param ui The pending remote observer.
   * @param callback Callback to confirm binding.
   */
  void BindObserver(mojo::PendingRemote<mojom::ServiceObserver> ui,
                    BindObserverCallback callback) override;

  /**
   * @brief Checks if content agent is allowed.
   *
   * @return True if allowed, false otherwise.
   */
  bool GetIsContentAgentAllowed() const;

  /**
   * @brief Sets whether content agent is allowed.
   *
   * @param is_allowed True to allow, false to disallow.
   */
  void SetIsContentAgentAllowed(bool is_allowed);

  /**
   * @brief Checks if the user has opted in.
   *
   * @return True if opted in, false otherwise.
   */
  bool HasUserOptedIn();

  /**
   * @brief Checks if the user has premium status.
   *
   * @return True if premium, false otherwise.
   */
  bool IsPremiumStatus();

  /**
   * @brief Checks if the AI Chat history feature is enabled.
   *
   * @return True if enabled, false otherwise.
   */
  bool IsAIChatHistoryEnabled();

  /**
   * @brief Gets the default AI engine consumer.
   *
   * @return A unique pointer to the `EngineConsumer`.
   */
  std::unique_ptr<EngineConsumer> GetDefaultAIEngine();

  /**
   * @brief Gets an engine consumer for a specific model.
   *
   * @param model_key The key of the model.
   * @return A unique pointer to the `EngineConsumer`.
   */
  std::unique_ptr<EngineConsumer> GetEngineForModel(
      const std::string& model_key);

  /**
   * @brief Gets an engine consumer for tab organization.
   *
   * @return A unique pointer to the `EngineConsumer`.
   */
  std::unique_ptr<EngineConsumer> GetEngineForTabOrganization();

  /**
   * @brief Sets the credential manager for testing.
   *
   * @param credential_manager Unique pointer to credential manager.
   */
  void SetCredentialManagerForTesting(
      std::unique_ptr<AIChatCredentialManager> credential_manager) {
    credential_manager_ = std::move(credential_manager);
  }

  /**
   * @brief Gets the credential manager for testing.
   *
   * @return Pointer to the credential manager.
   */
  AIChatCredentialManager* GetCredentialManagerForTesting() {
    return credential_manager_.get();
  }

  /**
   * @brief Gets the feedback API for testing.
   *
   * @return Pointer to the feedback API.
   */
  AIChatFeedbackAPI* GetFeedbackAPIForTesting() { return feedback_api_.get(); }

  /**
   * @brief Gets the count of in-memory conversations for testing.
   *
   * @return The count of conversations.
   */
  size_t GetInMemoryConversationCountForTesting();

  /**
   * @brief Gets the tab organization engine for testing.
   *
   * @return Pointer to the engine consumer.
   */
  EngineConsumer* GetTabOrganizationEngineForTesting() {
    return tab_organization_engine_.get();
  }

  /**
   * @brief Sets the tab organization engine for testing.
   *
   * @param engine Unique pointer to the engine consumer.
   */
  void SetTabOrganizationEngineForTesting(
      std::unique_ptr<EngineConsumer> engine) {
    tab_organization_engine_ = std::move(engine);
  }

  /**
   * @brief Sets the tab tracker service for testing.
   *
   * @param tab_tracker_service Pointer to the tab tracker service.
   */
  void SetTabTrackerServiceForTesting(TabTrackerService* tab_tracker_service) {
    tab_tracker_service_ = tab_tracker_service;
  }

  /**
   * @brief Sets the database for testing.
   *
   * @param db Sequence bound unique pointer to the database.
   */
  void SetDatabaseForTesting(
      base::SequenceBound<std::unique_ptr<AIChatDatabase>> db) {
    ai_chat_db_ = std::move(db);
  }

 private:
  friend class AIChatServiceUnitTest;

  // Key is uuid
  using ConversationMap =
      std::map<std::string, mojom::ConversationPtr, std::less<>>;
  using ConversationMapCallback = base::OnceCallback<void(ConversationMap&)>;

  void MaybeInitStorage();
  // Called when the database encryptor is ready.
  void OnOsCryptAsyncReady(os_crypt_async::Encryptor encryptor);
  void LoadConversationsLazy(ConversationMapCallback callback);
  void OnLoadConversationsLazyData(
      std::vector<mojom::ConversationPtr> conversations);
  void ReloadConversations(bool from_cancel = false);
  void OnConversationDataReceived(
      std::string conversation_uuid,
      base::OnceCallback<void(ConversationHandler*)> callback,
      mojom::ConversationArchivePtr data);

  void MaybeAssociateContent(
      ConversationHandler* conversation,
      int associated_content_id,
      base::WeakPtr<AssociatedContentDelegate> associated_content);

  // Determines whether a conversation could be unloaded.
  bool CanUnloadConversation(ConversationHandler* conversation);

  // If a conversation is unloadable, queues an event to unload it after a
  // delay. The delay is to allow for these situations:
  // - Primarily to guarantee that any references to the conversation during the
  // current stack frame will remain valid during the current stack frame.
  //   Solves this in a block:
  //       auto conversation = CreateConversation();
  //       conversation->SomeMethodThatTriggersMaybeUnload();
  //       /* conversation is unloaded */
  //       conversation->SomeOtherMethod(); // use after free!
  // - To give clients a chance to connect, which often happens in a separate
  // process, e.g. WebUI. This is not critical, but it avoids unloading and then
  // re-loading the conversation data whilst waiting for the UI to connect.
  void QueueMaybeUnloadConversation(ConversationHandler* conversation);

  // Unloads |conversation| if:
  // 1. It hasn't already been unloaded
  // 2. |CanUnloadConversation| is true
  void MaybeUnloadConversation(base::WeakPtr<ConversationHandler> conversation);
  void HandleFirstEntry(ConversationHandler* handler,
                        mojom::ConversationTurnPtr& entry,
                        std::optional<std::vector<std::string>> maybe_content,
                        mojom::ConversationPtr& conversation);
  void HandleNewEntry(
      ConversationHandler* handler,
      mojom::ConversationTurnPtr& entry,
      std::optional<std::vector<std::string>> maybe_associated_content,
      mojom::ConversationPtr& conversation);

  void OnUserOptedIn();
  void OnSkusServiceReceived(
      SkusServiceGetter getter,
      mojo::PendingRemote<skus::mojom::SkusService> service);
  void OnConversationListChanged();
  void OnPremiumStatusReceived(GetPremiumStatusCallback callback,
                               mojom::PremiumStatus status,
                               mojom::PremiumInfoPtr info);
  void OnDataDeletedForDisabledStorage(bool success);
  mojom::ServiceStatePtr BuildState();
  void OnStateChanged();
  void OnSkillsChanged();
  void OnMemoryEnabledChanged();
  void InitializeTools();

  void GetEngineForTabOrganization(base::OnceClosure callback);
  void ContinueGetEngineForTabOrganization(base::OnceClosure callback,
                                           mojom::PremiumStatus status,
                                           mojom::PremiumInfoPtr info);
  void GetSuggestedTopicsWithEngine(const std::vector<Tab>& tabs,
                                    GetSuggestedTopicsCallback callback);
  void GetFocusTabsWithEngine(const std::vector<Tab>& tabs,
                              const std::string& topic,
                              GetFocusTabsCallback callback);

  void OnSuggestedTopicsReceived(
      GetSuggestedTopicsCallback callback,
      base::expected<std::vector<std::string>, mojom::APIError> topics);
  void OnGetFocusTabs(
      GetFocusTabsCallback callback,
      base::expected<std::vector<std::string>, mojom::APIError> result);
  std::vector<std::unique_ptr<ToolProvider>>
  CreateToolProvidersForNewConversation();

  raw_ptr<ModelService> model_service_;
  raw_ptr<TabTrackerService> tab_tracker_service_;
  raw_ptr<PrefService> profile_prefs_;
  raw_ptr<AIChatMetrics> ai_chat_metrics_;
  raw_ptr<os_crypt_async::OSCryptAsync> os_crypt_async_;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  PrefChangeRegistrar pref_change_registrar_;

  std::unique_ptr<AIChatFeedbackAPI> feedback_api_;
  std::unique_ptr<AIChatCredentialManager> credential_manager_;

  // Factories of ToolProviders from other layers
  std::vector<std::unique_ptr<ToolProviderFactory>> tool_provider_factories_;

  // Engine for tab organization, created on demand and owned by AIChatService.
  std::unique_ptr<ai_chat::EngineConsumer> tab_organization_engine_;

  // Memory tool that is available and shared across all conversations.
  std::unique_ptr<MemoryStorageTool> memory_tool_;

  base::FilePath profile_path_;

  // Storage for conversations
  base::SequenceBound<std::unique_ptr<AIChatDatabase>> ai_chat_db_;

  // nullopt if haven't started fetching, empty if done fetching
  std::optional<std::vector<ConversationMapCallback>>
      on_conversations_loaded_callbacks_;
  base::OnceClosure cancel_conversation_load_callback_ = base::NullCallback();

  // All conversation metadata. Mainly just titles and uuids.
  ConversationMap conversations_;

  // Only keep ConversationHandlers around that are being
  // actively used. Any metadata that needs to stay in-memory
  // should be kept in |conversations_|. Any other data only for viewing
  // conversation detail should be persisted to database.
  // TODO(djandries): If the above requirement for this map changes,
  // adjust the metrics that depend on loaded conversation state accordingly.
  std::map<std::string, std::unique_ptr<ConversationHandler>>
      conversation_handlers_;

  // Map associated content id (a.k.a navigation id) to conversation uuid. This
  // acts as a cache for back-navigation to find the most recent conversation
  // for that navigation. This should be periodically cleaned up by removing any
  // keys where the ConversationHandler has had a destroyed
  // associated_content_delegate_ for some time.
  std::map<int, std::string> content_conversations_;

  // Cached suggested topics for users to be focused on from the latest
  // GetSuggestedTopics call, would be cleared when there are tab data changes.
  std::vector<std::string> cached_focus_topics_;

  base::ScopedMultiSourceObservation<ConversationHandler,
                                     ConversationHandler::Observer>
      conversation_observations_{this};
  mojo::ReceiverSet<mojom::Service> receivers_;
  mojo::RemoteSet<mojom::ServiceObserver> observer_remotes_;

  mojo::Receiver<mojom::TabDataObserver> tab_data_observer_receiver_{this};

  // AIChatCredentialManager / Skus does not provide an event when
  // subscription status changes. So we cache it and fetch latest fairly
  // often (whenever UI is focused).
  mojom::PremiumStatus last_premium_status_ = mojom::PremiumStatus::Unknown;

  // Whether conversations can utilize content agent capabilities. For now,
  // this is profile-specific.
  bool is_content_agent_allowed_ = false;

  base::WeakPtrFactory<AIChatService> weak_ptr_factory_{this};
};

}  // namespace ai_chat

#endif  // BRAVE_COMPONENTS_AI_CHAT_CORE_BROWSER_AI_CHAT_SERVICE_H_
