# Copyright Epic Games, Inc. All Rights Reserved.

cmake_minimum_required(VERSION 3.26)
project(MarketplaceKitWrapper LANGUAGES Swift)

add_library(MarketplaceKitWrapper STATIC MarketplaceKitWrapper.swift)
set_target_properties(MarketplaceKitWrapper PROPERTIES Swift_MODULE_NAME "MarketplaceKitWrapper")

add_custom_command(
	OUTPUT
		${CMAKE_CURRENT_SOURCE_DIR}/MarketplaceKitWrapper.h
	DEPENDS
		${CMAKE_CURRENT_SOURCE_DIR}/MarketplaceKitWrapper.swift
	WORKING_DIRECTORY
		${CMAKE_CURRENT_SOURCE_DIR}
	COMMAND
		${CMAKE_Swift_COMPILER}
		-frontend
		-typecheck
		-target ${CMAKE_OSX_ARCHITECTURES}-apple-ios${CMAKE_OSX_DEPLOYMENT_TARGET}
		-sdk ${CMAKE_OSX_SYSROOT}
		-module-name "MarketplaceKitWrapper"
		-emit-objc-header-path ${CMAKE_CURRENT_SOURCE_DIR}/MarketplaceKitWrapper.h
		MarketplaceKitWrapper.swift
	COMMAND_EXPAND_LISTS
)

add_custom_target(MarketplaceKitWrapperHeader
	DEPENDS
		${CMAKE_CURRENT_SOURCE_DIR}/MarketplaceKitWrapper.h
)

add_dependencies(MarketplaceKitWrapper MarketplaceKitWrapperHeader)
