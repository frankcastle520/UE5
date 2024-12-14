# !/bin/bash

set -e

FILES_TO_COPY=()

DoWork()
{
	echo "------------------------------------------------------------------------------"
	echo "Resigning with certificate: $DEVELOPER"

	rm -rf working
	if [[ "$SOURCEAPP" == *.ipa ]]; then
		echo "Prep: Unzipping $SOURCEAPP to 'working'..."
		unzip -qo "$SOURCEAPP" -d working
		TARGET="working/Payload/$(ls working/Payload/)"
	else
		if [[ "$TARGETAPP" == *.xcarchive ]]; then
			if [[ "$SOURCEAPP" != *.xcarchive ]]; then
				echo "When destination is a .xcarchive, the source must also be a .xcarchive"
				exit 1
			fi
			TARGET="$TARGETAPP/Products/Applications/$(ls $SOURCEAPP/Products/Applications/)"
			echo "Prep: Copying $SOURCEAPP to $TARGETAPP..."
			rm -rf "$TARGETAPP"
			ditto "$SOURCEAPP" "$TARGETAPP"
		else		
			if [[ "$SOURCEAPP" == *.xcarchive ]]; then
				SOURCEAPP="$SOURCEAPP/Products/Applications/$(ls $SOURCEAPP/Products/Applications/)"
			fi
			if [[ "$TARGETAPP" == *.ipa ]]; then
				mkdir -p working/Payload/
				TARGET="working/Payload/$(basename $SOURCEAPP)"
			else
				TARGET="$TARGETAPP"
			fi
			echo "Prep: Copying $SOURCEAPP to $TARGET..."
			rm -rf "$TARGET"
			ditto "$SOURCEAPP" "$TARGET"
		fi
	fi

	if [[ ! -z "$MOBILEPROV" ]]; then
		echo "Prep: Copying $SOURCEAPP into app..."
		cp "$MOBILEPROV" "$TARGET/embedded.mobileprovision"
	fi

	find -d "$TARGET"  \( -name "*.app" -o -name "*.appex" -o -name "*.framework" -o -name "*.dylib" \) > directories.txt
	oldbundleid=$(/usr/libexec/PlistBuddy -c "Print:CFBundleIdentifier" "$TARGET/Info.plist")

	if [[ ! -z "$BUNDLE" ]]; then
		echo "Changing BundleID from $oldbundleid with : $BUNDLE"	
		/usr/libexec/PlistBuddy -c "Set:CFBundleIdentifier $BUNDLE" "$TARGET/Info.plist"
	fi

	if [[ ! -z "$MARKETING_VERSION" ]]; then
		oldmarketingversion=$(/usr/libexec/PlistBuddy -c "Print:CFBundleShortVersionString" "$TARGET/Info.plist")	
		echo "Changing Marketing Version from $oldmarketingversion to $MARKETING_VERSION"
		/usr/libexec/PlistBuddy -c "Set:CFBundleShortVersionString $BUNDLE" "$TARGET/Info.plist"
	fi

	if [[ ! -z "$BUNDLE_VERSION" ]]; then
		oldbundleversion=$(/usr/libexec/PlistBuddy -c "Print:CFBundleVersion" "$TARGET/Info.plist")	
		echo "Changing Bundle Version from $oldbundleversion to $BUNDLE_VERSION"
		/usr/libexec/PlistBuddy -c "Set:CFBundleVersion $BUNDLE_VERSION" "$TARGET/Info.plist"
	fi

	if [[ ! -z "$CMDLINE" ]]; then
		echo "Setting commandline to $CMDLINE"
		echo $CMDLINE > "$TARGET/uecommandline.txt"
	fi
 
    for COPY_FILE_PATH in "${FILES_TO_COPY[@]}"
    do
        COPY_FILE_NAME=$(basename ${COPY_FILE_PATH});
        echo "Copying $COPY_FILE_PATH to $TARGET/$COPY_FILE_NAME"
        cp $COPY_FILE_PATH "$TARGET/$COPY_FILE_NAME"
    done

	echo "------------------------------------------------------------------------------"

	EXTRAOPTIONS=""
	while IFS='' read -r line || [[ -n "$line" ]]; do

		if [[ ! -z "$BUNDLE" ]] && [[ "$line" == *".appex"* ]]; then
		   extbundleid=$(/usr/libexec/PlistBuddy -c "Print:CFBundleIdentifier" "$line/Info.plist")
		   echo "Changing .appex BundleID $extbundleid with : ${extbundleid/$oldbundleid/$BUNDLE}"
		   /usr/libexec/PlistBuddy -c "Set:CFBundleIdentifier ${extbundleid/$oldbundleid/$BUNDLE}" "$line/Info.plist"
		fi    

		if [[ -f "$line/embedded.mobileprovision" ]]; then
			security cms -D -i "$line/embedded.mobileprovision" > t_entitlements_full.plist
			/usr/libexec/PlistBuddy -x -c 'Print:Entitlements' t_entitlements_full.plist > t_entitlements.plist
			TEAMID=$(/usr/libexec/PlistBuddy -c 'Print:com.apple.developer.team-identifier' t_entitlements.plist)
			/usr/libexec/PlistBuddy -c "Set:application-identifier $TEAMID.$BUNDLE" t_entitlements.plist
			
			if [[ ! -z "$BUNDLE" ]]; then
				EXTRAOPTIONS="--entitlements t_entitlements.plist"
			else
				EXTRAOPTIONS="--preserve-metadata=entitlements,flags,identifier"
			fi
		fi
		
		echo ""
		echo Codesigning $line [/usr/bin/codesign  --continue -f -s "$DEVELOPER" $EXTRAOPTIONS "$line"]
		/usr/bin/codesign --continue -f -s "$DEVELOPER" $EXTRAOPTIONS "$line"

	done < directories.txt

	if [[ "$TARGETAPP" == *.ipa ]]; then
		echo ""
		echo Zipping working to $TARGETAPP...
		cd working
		zip -qry ../working.ipa *
		cd ..
		mv working.ipa "$TARGETAPP"
		echo Cleanup up 'working' dir...
		rm -rf working
	elif [[ "$TARGETAPP" == *.xcarchive ]]; then
		echo ""
		echo Updating $TARGETAPP/Info.plist to match any new settings...
		if [[ ! -z "$BUNDLE" ]]; then
			echo "  Setting CFBundleIdentifier to $BUNDLE"
			/usr/libexec/PlistBuddy -c "Set:ApplicationProperties:CFBundleIdentifier $BUNDLE" "$TARGETAPP/Info.plist"
		fi

		echo "  Setting Team to $TEAMID"
		/usr/libexec/PlistBuddy -c "Set:ApplicationProperties:Team $TEAMID" "$TARGETAPP/Info.plist"
		FULLIDENTITY=$(security find-identity -v -p codesigning | grep "$DEVELOPER" | sed -n 's/.*\"\(.*\)\".*/\1/p;q')
		echo "  Setting SigningIdentity to $FULLIDENTITY"
		/usr/libexec/PlistBuddy -c "Set:ApplicationProperties:SigningIdentity $FULLIDENTITY" "$TARGETAPP/Info.plist"

		if [[ ! -z "$MARKETING_VERSION" ]]; then
		   echo "  Changing Marketing Version to $MARKETING_VERSION"
		   /usr/libexec/PlistBuddy -c "Set:ApplicationProperties:CFBundleShortVersionString $MARKETING_VERSION" "$TARGETAPP/Info.plist"
		fi
		
		if [[ ! -z "$BUNDLE_VERSION" ]]; then
			echo "  Changing Bundle Version to $BUNDLE_VERSION"
			/usr/libexec/PlistBuddy -c "Set:ApplicationProperties:CFBundleVersion $BUNDLE_VERSION" "$TARGETAPP/Info.plist"
		fi

	elif [[ "$SOURCEAPP" == *.ipa ]]; then
		echo Moving $TARGET to $TARGETAPP...
		mv "$TARGET" "$TARGETAPP"
	fi
	
	rm -f directories.txt
	rm -f t_entitlements_full.plist
	rm -f t_entitlements.plist
}

Help()
{
	echo ""
	echo "Usage: ResignApp <options>"
	echo ""
	echo "REQUIRED OPTIONS:"
	echo "  -s | --sourceapp <path to input>"
	echo "     Path to the .app or .ipa you want to resign"
	echo "  -d | --destapp <path to output>"
	echo "     Path to the output .app or .ipa (note that input type and output type do not need to match)"
	echo "  -i | --identity <signing identitiy> "
	echo "     Name of the identity to sign with (this is the full name or prefix of a certificate in your login keychain - 'Apple Development' will often work)"
	echo ""
	echo "OPTIONAL OPTIONS:"
	echo "  -p | --provision <path to .mobileprovision>"
	echo "     Path to the .mobileprovision file to sign with"
	echo "  -b | --bundleid"
	echo "     Bundle ID to use in the app, repleacing existing bundle ID"
	echo "  -mv | --marketingversion"
	echo "     Marketing version for the app. This is the version TestFlight/AppStoreConnect uses to manage builds"
	echo "  -bv | --bundleversion"
	echo "     Bundle version for the app. This is the internal version string that are gathered within one Marketing Version in TF/ASC"
	echo "  -c | --cmdline"
	echo "     A commandline to place into the app as uecommandline.txt"
    echo "  -cpy | --copy"
    echo "     Additional file to copy into the app, can be used multiple times. Copied files will be placed into the root directiory, without subdirectories"
	echo "  -h | --help"
	echo "     Show this message"
	exit 0
}


if [[ $# -eq 0 ]]; then
	Help
	exit 0
fi

while [[ $# -gt 0 ]]; do
  case $1 in
	-s|--sourceapp)
	  SOURCEAPP="$2"
	  shift # past argument
	  shift # past value
	  ;;
	-i|--identity)
	  DEVELOPER="$2"
	  shift # past argument
	  shift # past value
	  ;;
	-p|--provision)
	  MOBILEPROV="$2"
	  shift # past argument
	  shift # past value
	  ;;
	-d|--destapp)
	  TARGETAPP="$2"
	  shift # past argument
	  shift # past value
	  ;;
	-b|--bundleid)
	  BUNDLE="$2"
	  shift # past argument
	  shift # past value
	  ;;
	-mv|--marketingversion)
	  MARKETING_VERSION="$2"
	  shift # past argument
	  shift # past value
	  ;;
	-bv|--bundleversion)
	  BUNDLE_VERSION="$2"
	  shift # past argument
	  shift # past value
	  ;;
	-c|--cmdline)
	  CMDLINE="$2"
	  shift # past argument
	  shift # past value
	  ;;
    -cpy|--copy)
      FILES_TO_COPY+=("$2")
      shift # past argument
      shift # past value
      ;;
	-h|--help)
	  Help
	  shift # past value
	  ;;
	-*|--*)
	  echo "Unknown option $1"
	  exit 1
	  ;;
  esac
done

DoWork
