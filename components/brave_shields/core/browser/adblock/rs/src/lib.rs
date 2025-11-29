/* Copyright (c) 2023 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

//! This crate provides a cxx-based FFI for the
//! [adblock-rust](https://github.com/brave/adblock-rust) library.

mod convert;
mod engine;
mod filter_set;
mod resource_storage;
mod result;

use engine::*;
use filter_set::*;

#[allow(unsafe_op_in_unsafe_fn)]
#[cxx::bridge(namespace = adblock)]
mod ffi {
    extern "Rust" {
        /// Represents a set of filter lists.
        type FilterSet;

        /// Creates a new, empty filter set.
        ///
        /// # Returns
        ///
        /// A `Box<FilterSet>` containing the new filter set.
        fn new_filter_set() -> Box<FilterSet>;

        /// Adds a filter list to the filter set.
        ///
        /// # Arguments
        ///
        /// * `rules` - A vector of bytes representing the filter list content.
        ///
        /// # Returns
        ///
        /// A `FilterListMetadataResult` containing metadata about the added list.
        fn add_filter_list(&mut self, rules: &CxxVector<u8>) -> FilterListMetadataResult;

        /// Adds a filter list to the filter set with specific permissions.
        ///
        /// # Arguments
        ///
        /// * `rules` - A vector of bytes representing the filter list content.
        /// * `permission_mask` - A bitmask representing permissions for the rules.
        ///
        /// # Returns
        ///
        /// A `FilterListMetadataResult` containing metadata about the added list.
        fn add_filter_list_with_permissions(
            &mut self,
            rules: &CxxVector<u8>,
            permission_mask: u8,
        ) -> FilterListMetadataResult;
    }
    extern "Rust" {
        /// The ad blocking engine.
        type Engine;

        /// Creates a new engine with no rules.
        ///
        /// # Returns
        ///
        /// A `Box<Engine>` containing the new, empty engine.
        fn new_engine() -> Box<Engine>;

        /// Creates a new engine with rules from a given filter list.
        ///
        /// # Arguments
        ///
        /// * `rules` - A vector of bytes representing the filter list content.
        ///
        /// # Returns
        ///
        /// A `BoxEngineResult` containing the created engine or an error.
        fn engine_with_rules(rules: &CxxVector<u8>) -> BoxEngineResult;

        /// Creates a new engine with rules from a given filter set.
        ///
        /// # Arguments
        ///
        /// * `filter_set` - The `FilterSet` to use for creating the engine.
        ///
        /// # Returns
        ///
        /// A `BoxEngineResult` containing the created engine or an error.
        fn engine_from_filter_set(filter_set: Box<FilterSet>) -> BoxEngineResult;

        /// Configures the adblock domain resolver to use the `resolve_domain_position` implementation.
        ///
        /// # Returns
        ///
        /// `true` if the resolver was successfully set, `false` otherwise.
        fn set_domain_resolver() -> bool;

        /// Extracts the homepage and title from the metadata contained in a filter list.
        ///
        /// # Arguments
        ///
        /// * `list` - A vector of bytes representing the filter list content.
        ///
        /// # Returns
        ///
        /// A `FilterListMetadata` struct containing the extracted metadata.
        fn read_list_metadata(list: &CxxVector<u8>) -> FilterListMetadata;

        /// Enables a given tag for the engine.
        ///
        /// # Arguments
        ///
        /// * `tag` - The tag string to enable.
        fn enable_tag(&mut self, tag: &CxxString);

        /// Disables a given tag for the engine.
        ///
        /// # Arguments
        ///
        /// * `tag` - The tag string to disable.
        fn disable_tag(&mut self, tag: &CxxString);

        /// Returns true if a given tag is enabled for the engine.
        ///
        /// # Arguments
        ///
        /// * `key` - The tag string to check.
        ///
        /// # Returns
        ///
        /// `true` if the tag is enabled, `false` otherwise.
        fn tag_exists(&self, key: &CxxString) -> bool;

        /// Checks if a given request should be blocked and returns an evaluation result.
        ///
        /// # Arguments
        ///
        /// * `url` - The URL of the request.
        /// * `hostname` - The hostname of the request.
        /// * `source_hostname` - The hostname of the page initiating the request.
        /// * `request_type` - The type of request (e.g., "script", "image").
        /// * `third_party_request` - Whether the request is third-party.
        /// * `previously_matched_rule` - Whether a rule matched previously.
        /// * `force_check_exceptions` - Whether to force checking for exceptions.
        ///
        /// # Returns
        ///
        /// A `BlockerResult` struct with information on a matching rule and actions.
        fn matches(
            &self,
            url: &CxxString,
            hostname: &CxxString,
            source_hostname: &CxxString,
            request_type: &CxxString,
            third_party_request: bool,
            previously_matched_rule: bool,
            force_check_exceptions: bool,
        ) -> BlockerResult;

        /// Returns additional CSP directives to be added to a web response, if applicable.
        ///
        /// # Arguments
        ///
        /// * `url` - The URL of the request.
        /// * `hostname` - The hostname of the request.
        /// * `source_hostname` - The hostname of the page initiating the request.
        /// * `request_type` - The type of request.
        /// * `third_party_request` - Whether the request is third-party.
        ///
        /// # Returns
        ///
        /// A `String` containing the CSP directives.
        fn get_csp_directives(
            &self,
            url: &CxxString,
            hostname: &CxxString,
            source_hostname: &CxxString,
            request_type: &CxxString,
            third_party_request: bool,
        ) -> String;

        /// Serializes the engine state to a byte vector.
        ///
        /// # Returns
        ///
        /// A `Vec<u8>` containing the serialized engine.
        pub fn serialize(&self) -> Vec<u8>;

        /// Deserializes and loads a binary-serialized Engine.
        ///
        /// # Arguments
        ///
        /// * `serialized` - The serialized engine data.
        ///
        /// # Returns
        ///
        /// `true` if deserialization was successful, `false` otherwise.
        fn deserialize(&mut self, serialized: &CxxVector<u8>) -> bool;

        /// Loads JSON-serialized resources into the engine resource set.
        ///
        /// # Arguments
        ///
        /// * `resources_json` - A JSON string representing the resources.
        ///
        /// # Returns
        ///
        /// `true` if resources were loaded successfully, `false` otherwise.
        fn use_resources(&mut self, resources_json: &CxxString) -> bool;

        /// Returns JSON-serialized cosmetic filter resources for a given url.
        ///
        /// # Arguments
        ///
        /// * `url` - The URL to check for cosmetic resources.
        ///
        /// # Returns
        ///
        /// A `String` containing the JSON-serialized cosmetic resources.
        fn url_cosmetic_resources(&self, url: &CxxString) -> String;

        /// Returns list of CSS selectors that require a generic CSS hide rule.
        ///
        /// # Arguments
        ///
        /// * `classes` - A list of class names on the page.
        /// * `ids` - A list of IDs on the page.
        /// * `exceptions` - A list of exception rules.
        ///
        /// # Returns
        ///
        /// A `VecStringResult` containing the list of CSS selectors.
        fn hidden_class_id_selectors(
            &self,
            classes: &CxxVector<CxxString>,
            ids: &CxxVector<CxxString>,
            exceptions: &CxxVector<CxxString>,
        ) -> VecStringResult;

        /// Returns the blocker debug info containing regex info.
        ///
        /// # Returns
        ///
        /// A `DebugInfo` struct containing regex data.
        fn get_debug_info(&self) -> DebugInfo;

        /// Removes a regex entry by the id.
        ///
        /// # Arguments
        ///
        /// * `regex_id` - The ID of the regex to remove.
        fn discard_regex(&mut self, regex_id: u64);

        /// Sets a discard policy for the regex manager.
        ///
        /// # Arguments
        ///
        /// * `new_discard_policy` - The new policy to apply.
        fn set_regex_discard_policy(&mut self, new_discard_policy: &RegexManagerDiscardPolicy);

        /// Converts a list in adblock syntax to its corresponding iOS content-blocking syntax.
        ///
        /// `truncated` will be set to indicate whether or not some rules had to be removed
        /// to avoid iOS's maximum rule count limit.
        ///
        /// # Arguments
        ///
        /// * `rules` - The adblock rules to convert.
        ///
        /// # Returns
        ///
        /// A `ContentBlockingRulesResult` containing the converted rules.
        fn convert_rules_to_content_blocking(rules: &CxxString) -> ContentBlockingRulesResult;
    }

    unsafe extern "C++" {
        include!("brave/components/brave_shields/core/browser/adblock/resolver/adblock_domain_resolver.h");

        /// Wrapper function for net::registry_controlled_domains::GetDomainAndRegistry.
        ///
        /// # Arguments
        ///
        /// * `host` - The host string to resolve.
        ///
        /// # Returns
        ///
        /// A `DomainPosition` struct indicating the start and end of the domain.
        fn resolve_domain_position(host: &CxxString) -> DomainPosition;
    }

    /// Represents the position of the domain in a host string.
    struct DomainPosition {
        start: u32,
        end: u32,
    }

    /// The result of checking if a request should be blocked.
    #[derive(Default)]
    struct BlockerResult {
        matched: bool,
        important: bool,
        has_exception: bool,
        redirect: OptionalString,
        rewritten_url: OptionalString,
    }

    /// Debug information for a regex entry.
    struct RegexDebugEntry {
        id: u64,
        regex: OptionalString,
        unused_secs: u64,
        usage_count: usize,
    }

    /// Debug information for the adblock engine.
    struct DebugInfo {
        regex_data: Vec<RegexDebugEntry>,
        compiled_regex_count: usize,
        flatbuffer_size: usize,
    }

    /// Policy for discarding unused regexes.
    struct RegexManagerDiscardPolicy {
        cleanup_interval_secs: u64,
        discard_unused_secs: u64,
    }

    /// Metadata extracted from a filter list.
    #[derive(Default)]
    struct FilterListMetadata {
        homepage: OptionalString,
        title: OptionalString,
        expires_hours: OptionalU16,
    }

    /// Rules converted to iOS content blocking format.
    #[derive(Default)]
    struct ContentBlockingRules {
        rules_json: String,
        truncated: bool,
    }

    /// The kind of result returned by FFI functions.
    enum ResultKind {
        Success,
        JsonError,
        Utf8Error,
        AdblockError,
    }

    // The following structs are not generic because generic type parameters are
    // not yet supported in cxx.
    // Created custom Result structs because cxx auto converts Result<T> to
    // std::exception, and exceptions are not allowed in Chromium.

    /// Result wrapper for `ContentBlockingRules`.
    struct ContentBlockingRulesResult {
        value: ContentBlockingRules,
        result_kind: ResultKind,
        error_message: String,
    }

    /// Result wrapper for a vector of strings.
    struct VecStringResult {
        value: Vec<String>,
        result_kind: ResultKind,
        error_message: String,
    }

    /// Result wrapper for a `Box<Engine>`.
    struct BoxEngineResult {
        value: Box<Engine>,
        result_kind: ResultKind,
        error_message: String,
    }

    /// Result wrapper for `FilterListMetadata`.
    struct FilterListMetadataResult {
        value: FilterListMetadata,
        result_kind: ResultKind,
        error_message: String,
    }

    // Created custom Option struct because automatic conversion of Option<T>
    // is not yet supported in cxx.
    /// Custom Option struct for String.
    #[derive(Default)]
    struct OptionalString {
        has_value: bool,
        value: String,
    }

    /// Custom Option struct for u16.
    #[derive(Default)]
    struct OptionalU16 {
        has_value: bool,
        value: u16,
    }
}
