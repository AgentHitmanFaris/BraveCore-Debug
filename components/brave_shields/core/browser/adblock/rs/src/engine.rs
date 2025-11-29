/* Copyright (c) 2023 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

use std::collections::HashSet;
use std::str::Utf8Error;
use std::sync::Arc;

use crate::resource_storage::BraveCoreResourceStorage;
use adblock::lists::FilterSet as InnerFilterSet;
use adblock::resources::{InMemoryResourceStorage, Resource};
use adblock::url_parser::ResolvesDomain;
use adblock::Engine as InnerEngine;
use cxx::{let_cxx_string, CxxString, CxxVector};

use crate::ffi::{
    resolve_domain_position, BlockerResult, BoxEngineResult, ContentBlockingRulesResult, DebugInfo,
    FilterListMetadata, RegexManagerDiscardPolicy, VecStringResult,
};
use crate::filter_set::FilterSet;
use crate::result::InternalError;

#[cfg(feature = "ios")]
use crate::ffi::ContentBlockingRules;

/// Wrapper around the adblock engine.
pub struct Engine {
    engine: InnerEngine,
}

impl Default for Box<Engine> {
    fn default() -> Self {
        new_engine()
    }
}

/// Creates a new engine with no rules.
///
/// # Returns
///
/// A `Box<Engine>` containing the new, empty engine.
pub fn new_engine() -> Box<Engine> {
    Box::new(Engine { engine: InnerEngine::default() })
}

/// Creates a new engine with rules from a given filter list.
///
/// # Arguments
///
/// * `rules` - A vector of bytes representing the filter list content.
///
/// # Returns
///
/// A `BoxEngineResult` containing the created engine or an error if parsing failed.
pub fn engine_with_rules(rules: &CxxVector<u8>) -> BoxEngineResult {
    || -> Result<Box<Engine>, InternalError> {
        let mut filter_set = InnerFilterSet::new(false);
        filter_set.add_filter_list(std::str::from_utf8(rules.as_slice())?, Default::default());
        let engine = InnerEngine::from_filter_set(filter_set, true);
        Ok(Box::new(Engine { engine }))
    }()
    .into()
}

/// Creates a new engine with rules from a given filter set.
///
/// # Arguments
///
/// * `filter_set` - The `FilterSet` to use for creating the engine.
///
/// # Returns
///
/// A `BoxEngineResult` containing the created engine or an error.
pub fn engine_from_filter_set(filter_set: Box<FilterSet>) -> BoxEngineResult {
    || -> Result<Box<Engine>, InternalError> {
        let engine = InnerEngine::from_filter_set(filter_set.0, true);
        Ok(Box::new(Engine { engine }))
    }()
    .into()
}

struct DomainResolver;

impl ResolvesDomain for DomainResolver {
    fn get_host_domain(&self, host: &str) -> (usize, usize) {
        let_cxx_string!(host_cxx_string = host);
        let position = resolve_domain_position(&host_cxx_string);
        (position.start as usize, position.end as usize)
    }
}

/// Configures the adblock domain resolver to use the `resolve_domain_position` implementation.
///
/// # Returns
///
/// `true` if the resolver was successfully set, `false` otherwise.
pub fn set_domain_resolver() -> bool {
    adblock::url_parser::set_domain_resolver(Box::new(DomainResolver)).is_ok()
}

/// Extracts the homepage and title from the metadata contained in a filter list.
///
/// # Arguments
///
/// * `list` - A vector of bytes representing the filter list content.
///
/// # Returns
///
/// A `FilterListMetadata` struct containing the extracted metadata.
pub fn read_list_metadata(list: &CxxVector<u8>) -> FilterListMetadata {
    std::str::from_utf8(list.as_slice())
        .map(|list| adblock::lists::read_list_metadata(list).into())
        .unwrap_or_default()
}

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
#[cfg(feature = "ios")]
pub fn convert_rules_to_content_blocking(rules: &CxxString) -> ContentBlockingRulesResult {
    || -> Result<ContentBlockingRules, InternalError> {
        use adblock::lists::{ParseOptions, RuleTypes};

        /// This value corresponds to `maxRuleCount` here:
        /// https://github.com/WebKit/WebKit/blob/4a2df13be2253f64d8da58b794d74347a3742652/Source/WebCore/contentextensions/ContentExtensionParser.cpp#L299
        const MAX_CB_LIST_SIZE: usize = 150000;

        let mut filter_set = InnerFilterSet::new(true);
        filter_set.add_filter_list(
            rules.to_str()?,
            ParseOptions { rule_types: RuleTypes::NetworkOnly, ..Default::default() },
        );

        // `unwrap` is safe here because `into_content_blocking` only panics if the
        // `FilterSet` was not created in debug mode
        let (mut cb_rules, _) = filter_set.into_content_blocking().unwrap();
        let rules_len = cb_rules.len();
        let truncated = if rules_len > MAX_CB_LIST_SIZE {
            // Note that the last rule is always the first-party document exception rule,
            // which we want to keep. Otherwise, we can arbitrarily truncate rules
            // before that to ensure that the list can actually compile.
            cb_rules.swap(rules_len - 1, MAX_CB_LIST_SIZE - 1);
            cb_rules.truncate(MAX_CB_LIST_SIZE);
            true
        } else {
            false
        };
        Ok(ContentBlockingRules { rules_json: serde_json::to_string(&cb_rules)?, truncated })
    }()
    .into()
}

/// Converts a list in adblock syntax to its corresponding iOS content-blocking syntax.
/// This function panics if called on non-iOS platforms.
#[cfg(not(feature = "ios"))]
pub fn convert_rules_to_content_blocking(_rules: &CxxString) -> ContentBlockingRulesResult {
    panic!("convert_rules_to_content_blocking can only be called on iOS");
}

fn convert_cxx_string_vector_to_string_collection<C>(
    value: &CxxVector<CxxString>,
) -> Result<C, Utf8Error>
where
    C: FromIterator<String>,
{
    value.iter().map(|s| s.to_str().map(|t| t.to_string())).collect()
}

impl Engine {
    /// Enables a given tag for the engine.
    ///
    /// # Arguments
    ///
    /// * `tag` - The tag string to enable.
    pub fn enable_tag(&mut self, tag: &CxxString) {
        self.engine.enable_tags(&[tag.to_str().unwrap()])
    }

    /// Disables a given tag for the engine.
    ///
    /// # Arguments
    ///
    /// * `tag` - The tag string to disable.
    pub fn disable_tag(&mut self, tag: &CxxString) {
        self.engine.disable_tags(&[tag.to_str().unwrap()])
    }

    /// Returns true if a given tag is enabled for the engine.
    ///
    /// # Arguments
    ///
    /// * `key` - The tag string to check.
    ///
    /// # Returns
    ///
    /// `true` if the tag is enabled, `false` otherwise.
    pub fn tag_exists(&self, key: &CxxString) -> bool {
        self.engine.tag_exists(key.to_str().unwrap())
    }

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
    pub fn matches(
        &self,
        url: &CxxString,
        hostname: &CxxString,
        source_hostname: &CxxString,
        request_type: &CxxString,
        third_party_request: bool,
        previously_matched_rule: bool,
        force_check_exceptions: bool,
    ) -> BlockerResult {
        // The following strings are guaranteed to be
        // UTF-8, so unwrapping directly should be okay.
        self.engine
            .check_network_request_subset(
                &adblock::request::Request::preparsed(
                    url.to_str().unwrap(),
                    hostname.to_str().unwrap(),
                    source_hostname.to_str().unwrap(),
                    request_type.to_str().unwrap(),
                    third_party_request,
                ),
                previously_matched_rule,
                force_check_exceptions,
            )
            .into()
    }

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
    pub fn get_csp_directives(
        &self,
        url: &CxxString,
        hostname: &CxxString,
        source_hostname: &CxxString,
        request_type: &CxxString,
        third_party_request: bool,
    ) -> String {
        // The following strings are also UTF-8.
        self.engine
            .get_csp_directives(&adblock::request::Request::preparsed(
                url.to_str().unwrap(),
                hostname.to_str().unwrap(),
                source_hostname.to_str().unwrap(),
                request_type.to_str().unwrap(),
                third_party_request,
            ))
            .unwrap_or_default()
    }

    /// Serializes the engine state to a byte vector.
    ///
    /// A 0-length vector will be returned if there was any issue during
    /// serialization. Be sure to handle that case.
    ///
    /// # Returns
    ///
    /// A `Vec<u8>` containing the serialized engine.
    pub fn serialize(&self) -> Vec<u8> {
        self.engine.serialize()
    }

    /// Deserializes and loads a binary-serialized Engine.
    ///
    /// # Arguments
    ///
    /// * `serialized` - The serialized engine data.
    ///
    /// # Returns
    ///
    /// `true` if deserialization was successful, `false` otherwise.
    pub fn deserialize(&mut self, serialized: &CxxVector<u8>) -> bool {
        self.engine.deserialize(serialized.as_slice()).is_ok()
    }

    /// Loads JSON-serialized resources into the engine resource set.
    ///
    /// # Arguments
    ///
    /// * `resources_json` - A JSON string representing the resources.
    ///
    /// # Returns
    ///
    /// `true` if resources were loaded successfully, `false` otherwise.
    // TODO(https://github.com/brave/brave-browser/issues/50368): remove this method, expose storage API.
    pub fn use_resources(&mut self, resources_json: &CxxString) -> bool {
        resources_json
            .to_str()
            .ok()
            .and_then(|resources_json| serde_json::from_str::<Vec<Resource>>(resources_json).ok())
            .and_then(|resources| {
                let in_memory_storage = InMemoryResourceStorage::from_resources(resources);
                let shared_storage = Arc::new(in_memory_storage);
                let bc_storage = BraveCoreResourceStorage { shared_storage };

                self.engine.use_resource_storage(bc_storage);
                Some(())
            })
            .is_some()
    }

    /// Returns JSON-serialized cosmetic filter resources for a given url.
    ///
    /// # Arguments
    ///
    /// * `url` - The URL to check for cosmetic resources.
    ///
    /// # Returns
    ///
    /// A `String` containing the JSON-serialized cosmetic resources.
    pub fn url_cosmetic_resources(&self, url: &CxxString) -> String {
        let resources = self.engine.url_cosmetic_resources(url.to_str().unwrap());
        serde_json::to_string(&resources).unwrap()
    }

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
    pub fn hidden_class_id_selectors(
        &self,
        classes: &CxxVector<CxxString>,
        ids: &CxxVector<CxxString>,
        exceptions: &CxxVector<CxxString>,
    ) -> VecStringResult {
        || -> Result<Vec<String>, InternalError> {
            let classes: Vec<String> = convert_cxx_string_vector_to_string_collection(classes)?;
            let ids: Vec<String> = convert_cxx_string_vector_to_string_collection(ids)?;
            let exceptions: HashSet<String> =
                convert_cxx_string_vector_to_string_collection(exceptions)?;
            Ok(self.engine.hidden_class_id_selectors(&classes, &ids, &exceptions))
        }()
        .into()
    }

    /// Returns the blocker debug info containing regex info.
    ///
    /// # Returns
    ///
    /// A `DebugInfo` struct containing regex data.
    pub fn get_debug_info(&self) -> DebugInfo {
        self.engine.get_debug_info().into()
    }

    /// Removes a regex entry by the id.
    ///
    /// # Arguments
    ///
    /// * `regex_id` - The ID of the regex to remove.
    pub fn discard_regex(&mut self, regex_id: u64) {
        self.engine.discard_regex(regex_id)
    }

    /// Sets a discard policy for the regex manager.
    ///
    /// # Arguments
    ///
    /// * `new_discard_policy` - The new policy to apply.
    pub fn set_regex_discard_policy(&mut self, new_discard_policy: &RegexManagerDiscardPolicy) {
        self.engine.set_regex_discard_policy(new_discard_policy.into())
    }
}
