    ///////////////////////////////////////////////////////////////////////
    // #1513 Scaleform 4.6.34 AMF3 codec (ByteArray.writeObject 00e1c860,
    // readObject 00e1c140). Output is always AMF3 and big-endian; objectEncoding
    // is not consulted in either direction.

    struct ByteArray::AMF3WriteScope
    {
        // 4.6.34 writer scope: string and object tables. Its traits table is
        // only looked up (e745a3), never filled, so traits are always inline.
        HashLH<ASStringNode*, UInt32> StringIndex;
        ArrayLH<Ptr<ASStringNode> > Strings;
        HashLH<AS3::Object*, UInt32> ObjectIndex;
        ArrayLH<SPtr<AS3::Object> > Objects;
    };

    struct ByteArray::AMF3ReadScope
    {
        // 4.6.34 reader context: strings, objects, traits and sealed members.
        // A traits record pushes its members before its names, its traits after.
        ArrayLH<Ptr<ASStringNode> > Strings;
        ArrayLH<SPtr<AS3::Object> > Objects;
        ArrayLH<SPtr<InstanceTraits::Traits> > Traits;
        ArrayLH<UPInt> MemberStart;
        ArrayLH<UPInt> MemberCount;
        ArrayLH<const SlotInfo*> Members;
    };

    static bool IsAMF3FunctionObject(const Value& v)
    {
        // ea06d0
        return v.GetKind() == Value::kObject && v.GetObject() &&
            v.GetObject()->GetTraitsType() == Traits_Function;
    }

    static const ClassTraits::Traits* AMF3BuiltinTraits(VM& vm, const TypeInfo& ti)
    {
        // e58e80 resolves builtins in the system domain.
        return vm.Resolve2ClassTraits(ti, vm.GetSystemAppDomain());
    }

    static bool IsAMF3SealedMember(VM& vm, const SlotInfo& si)
    {
        // e75bd0: public data slots and read-write accessors that are neither
        // read-only (flag bits 0-1, the e44900 #1074 test) nor const.
        const SlotInfo::BindingType bt = si.GetBindingType();
        if (!(bt < SlotInfo::BT_ConstChar || bt == SlotInfo::BT_GetSet))
            return false;
        if (si.IsReadonly())
            return false;
        const Abc::TraitInfo* ti = si.GetTraitInfoPtr();
        if (ti && ti->GetType() == Abc::TraitInfo::tConst)
            return false;
        return &si.GetNamespace() == &vm.GetPublicNamespace();
    }

    static ASString AMF3MemberName(VM& vm, const ASString& name)
    {
        // e45600: the text after the last '/', else after the last '.'.
        const char* text = name.ToCStr();
        const char* cut = strrchr(text, '/');
        if (!cut)
            cut = strrchr(text, '.');
        if (!cut)
            return name;
        return vm.GetStringManager().CreateString(cut + 1);
    }

    void ByteArray::AMF3WriteByte(UInt8 v)
    {
        Write(&v, 1);
    }

    void ByteArray::AMF3WriteU29(UInt32 v)
    {
        // e73a30: the four-byte form is accepted below 2^30, so bit 29 is lost
        // in [2^29, 2^30); larger values are RangeError #1506.
        if (v < 0x80)
            AMF3WriteByte(static_cast<UInt8>(v));
        else if (v < 0x4000)
        {
            AMF3WriteByte(static_cast<UInt8>((v >> 7) | 0x80));
            AMF3WriteByte(static_cast<UInt8>(v & 0x7f));
        }
        else if (v < 0x200000)
        {
            AMF3WriteByte(static_cast<UInt8>((v >> 14) | 0x80));
            AMF3WriteByte(static_cast<UInt8>((v >> 7) | 0x80));
            AMF3WriteByte(static_cast<UInt8>(v & 0x7f));
        }
        else if (v < 0x40000000)
        {
            AMF3WriteByte(static_cast<UInt8>((v >> 22) | 0x80));
            AMF3WriteByte(static_cast<UInt8>((v >> 15) | 0x80));
            AMF3WriteByte(static_cast<UInt8>((v >> 8) | 0x80));
            AMF3WriteByte(static_cast<UInt8>(v & 0xff));
        }
        else
        {
            VM& vm = GetVM();
            vm.ThrowRangeError(VM::Error(VM::eInvalidRangeError, vm));
        }
    }

    void ByteArray::AMF3WriteU32(UInt32 v)
    {
        const UInt8 bytes[4] = {static_cast<UInt8>(v >> 24), static_cast<UInt8>(v >> 16),
                                static_cast<UInt8>(v >> 8), static_cast<UInt8>(v)};
        Write(bytes, 4);
    }

    void ByteArray::AMF3WriteDouble(Value::Number v)
    {
        UInt64 bits;
        memcpy(&bits, &v, sizeof(bits));
        AMF3WriteU32(static_cast<UInt32>(bits >> 32));
        AMF3WriteU32(static_cast<UInt32>(bits));
    }

    void ByteArray::AMF3WriteString(AMF3WriteScope& scope, const ASString& s)
    {
        // e73c20: keyed by string node; the empty string is never a reference.
        ASStringNode* node = s.GetNode();
        if (node->Size == 0)
        {
            AMF3WriteByte(0x01);
            return;
        }
        const UInt32* found = scope.StringIndex.Get(node);
        if (found)
        {
            AMF3WriteU29(*found << 1);
            return;
        }
        scope.StringIndex.Set(node, static_cast<UInt32>(scope.Strings.GetSize()));
        scope.Strings.PushBack(Ptr<ASStringNode>(node));
        AMF3WriteU29((static_cast<UInt32>(node->Size) << 1) | 1);
        if (!GetVM().IsException())
            Write(node->pData, static_cast<UInt32>(node->Size));
    }

    bool ByteArray::AMF3WriteObjectRef(AMF3WriteScope& scope, UInt8 marker, AS3::Object& o)
    {
        // e9f0b0/e9d3e0: an object enters the table after its marker and
        // before its contents, so cycles and shared objects become references.
        AMF3WriteByte(marker);
        const UInt32* found = scope.ObjectIndex.Get(&o);
        if (found)
        {
            AMF3WriteU29(*found << 1);
            return true;
        }
        scope.ObjectIndex.Set(&o, static_cast<UInt32>(scope.Objects.GetSize()));
        scope.Objects.PushBack(SPtr<AS3::Object>(&o));
        return false;
    }

    bool ByteArray::AMF3WriteGeneric(AMF3WriteScope& scope, AS3::Object& o)
    {
        // e744c0
        VM& vm = GetVM();
        if (AMF3WriteObjectRef(scope, 0x0a, o))
            return !vm.IsException();
        Traits& tr = o.GetTraits();
        const UPInt slotNum = tr.GetSlotInfoNum();
        UInt32 sealed = 0;
        for (UPInt i = 0; i < slotNum; ++i)
            if (IsAMF3SealedMember(vm, tr.GetSlotInfo(AbsoluteIndex(i))))
                ++sealed;
        const bool dynamic = tr.IsDynamic();
        const ClassTraits::Traits* externalizable = AMF3BuiltinTraits(vm, AS3::fl_utils::IExternalizableTI);
        const bool ext = externalizable && externalizable->IsParentTypeOf(vm.GetClassTraits(Value(&o)));
        AMF3WriteU29(3 | (UInt32(ext) << 2) | (UInt32(dynamic) << 3) | (sealed << 4));
        if (vm.IsException())
            return false;
        AMF3WriteString(scope, vm.GetAliasByClass(tr.GetConstructor()));
        for (UPInt i = 0; i < slotNum; ++i)
            if (IsAMF3SealedMember(vm, tr.GetSlotInfo(AbsoluteIndex(i))))
                AMF3WriteString(scope, AMF3MemberName(vm, tr.GetSlotName(AbsoluteIndex(i))));
        if (ext)
        {
            // e74742: writeExternal(this); nested writes share this scope.
            const Multiname mn(vm.GetPublicNamespace(), vm.GetStringManager().CreateConstString("writeExternal"));
            const Value arg(this);
            Value result;
            const bool saved = AMF3InWriteExternal;
            AMF3InWriteExternal = true;
            const bool ok = AS3::ExecutePropertyUnsafe(vm, mn, Value(&o), result, 1, &arg);
            AMF3InWriteExternal = saved;
            return ok;
        }
        for (UPInt i = 0; i < slotNum; ++i)
        {
            const SlotInfo& si = tr.GetSlotInfo(AbsoluteIndex(i));
            if (!IsAMF3SealedMember(vm, si))
                continue;
            Value v;
            if (!si.GetSlotValueUnsafe(v, &o) || !AMF3WriteValue(scope, v))
                return false;
        }
        if (!dynamic)
            return true;
        // Functions and empty names are skipped; DontEnum is not checked.
        const AS3::Object::DynAttrsType* attrs = o.GetDynamicAttrs();
        for (AS3::Object::DynAttrsType::ConstIterator it = attrs->Begin(); !it.IsEnd(); ++it)
        {
            if (IsAMF3FunctionObject(it->Second) || it->First.GetName().IsEmpty())
                continue;
            AMF3WriteString(scope, it->First.GetName());
            if (!AMF3WriteValue(scope, it->Second))
                return false;
        }
        AMF3WriteString(scope, vm.GetStringManager().CreateEmptyString());
        return !vm.IsException();
    }

    bool ByteArray::AMF3WriteArray(AMF3WriteScope& scope, Instances::fl::Array& a)
    {
        // e73e47: the dense count is the contiguous part; later elements are
        // decimal keys of the associative part. Named properties are not written.
        VM& vm = GetVM();
        if (AMF3WriteObjectRef(scope, 0x09, a))
            return !vm.IsException();
        const ValueArrayDH& dense = a.GetContiguousPart();
        const UPInt denseSize = dense.GetSize();
        AMF3WriteU29((static_cast<UInt32>(denseSize) << 1) | 1);
        if (a.GetSize() > denseSize)
        {
            for (AbsoluteIndex i = a.GetNextArrayIndex(AbsoluteIndex(-1)); i.IsValid();
                 i = a.GetNextArrayIndex(i))
            {
                if (i.Get() < denseSize || IsAMF3FunctionObject(a.At(i.Get())))
                    continue;
                char key[16];
                SFsprintf(key, sizeof(key), "%u", static_cast<unsigned>(i.Get()));
                AMF3WriteString(scope, vm.GetStringManager().CreateString(key));
                if (!AMF3WriteValue(scope, a.At(i.Get())))
                    return false;
            }
        }
        AMF3WriteString(scope, vm.GetStringManager().CreateEmptyString());
        for (UPInt i = 0; i < denseSize; ++i)
        {
            if (IsAMF3FunctionObject(dense[i]))
                AMF3WriteByte(0x00);
            else if (!AMF3WriteValue(scope, dense[i]))
                return false;
        }
        return !vm.IsException();
    }

    bool ByteArray::AMF3WriteObject(AMF3WriteScope& scope, AS3::Object& o)
    {
        // e73e10: the builtin type selects the marker; everything else,
        // including class objects, QName and XMLList, is a generic object.
        VM& vm = GetVM();
        Traits& tr = o.GetTraits();
        if (tr.IsClassTraits())
            return AMF3WriteGeneric(scope, o);
        switch (tr.GetTraitsType())
        {
        case Traits_Array:
            return AMF3WriteArray(scope, static_cast<Instances::fl::Array&>(o));
        case Traits_Date:
            if (AMF3WriteObjectRef(scope, 0x08, o))
                return !vm.IsException();
            AMF3WriteByte(0x01);
            AMF3WriteDouble(static_cast<Instances::fl::Date&>(o).TimeValue);
            return true;
        case Traits_Function:
            return false;
        case Traits_Dictionary:
            {
                // e74dd0
                Instances::fl_utils::Dictionary& d = static_cast<Instances::fl_utils::Dictionary&>(o);
                if (AMF3WriteObjectRef(scope, 0x11, o))
                    return !vm.IsException();
                AMF3WriteU29((static_cast<UInt32>(d.ValueH.GetSize()) << 1) | 1);
                AMF3WriteByte(d.WeakKeys ? 1 : 0);
                for (ValueContainerType::ConstIterator it = d.ValueH.Begin(); !it.IsEnd(); ++it)
                    if (!AMF3WriteValue(scope, it->First) || !AMF3WriteValue(scope, it->Second))
                        return false;
                return !vm.IsException();
            }
        case Traits_Vector_object:
            {
                // e75520: the element type is its class alias.
                Instances::fl_vec::Vector_object& v = static_cast<Instances::fl_vec::Vector_object&>(o);
                if (AMF3WriteObjectRef(scope, 0x10, o))
                    return !vm.IsException();
                const UInt32 size = v.GetArrayBase().GetArraySize();
                AMF3WriteU29((size << 1) | 1);
                AMF3WriteByte(v.GetArrayBase().GetFixed() ? 1 : 0);
                AMF3WriteString(scope, vm.GetAliasByClass(
                    const_cast<InstanceTraits::Traits&>(v.GetEnclosedClassTraits().GetInstanceTraits()).GetConstructor()));
                for (UInt32 i = 0; i < size; ++i)
                {
                    Value element;
                    v.Get(i, element);
                    if (!AMF3WriteValue(scope, element))
                        return false;
                }
                return !vm.IsException();
            }
        case Traits_XML:
            {
                // e743c1: the XML text, outside the string table.
                if (AMF3WriteObjectRef(scope, 0x0b, o))
                    return !vm.IsException();
                ASString text = vm.GetStringManager().CreateEmptyString();
                static_cast<Instances::fl::XML&>(o).AS3toXMLString(text);
                if (vm.IsException())
                    return false;
                AMF3WriteU29((static_cast<UInt32>(text.GetSize()) << 1) | 1);
                if (!vm.IsException())
                    Write(text.ToCStr(), static_cast<UInt32>(text.GetSize()));
                return !vm.IsException();
            }
        default:
            break;
        }
        const ClassTraits::Traits* byteArray = AMF3BuiltinTraits(vm, AS3::fl_utils::ByteArrayTI);
        if (byteArray && byteArray->IsParentTypeOf(vm.GetClassTraits(Value(&o))))
        {
            ByteArray& b = static_cast<ByteArray&>(o);
            if (AMF3WriteObjectRef(scope, 0x0c, o))
                return !vm.IsException();
            AMF3WriteU29((b.Length << 1) | 1);
            if (!vm.IsException())
                Write(b.Data.GetDataPtr(), b.Length);
            return !vm.IsException();
        }
        if (&tr == &vm.GetITraitsVectorSInt() || &tr == &vm.GetITraitsVectorUInt())
        {
            const bool sint = &tr == &vm.GetITraitsVectorSInt();
            if (AMF3WriteObjectRef(scope, sint ? 0x0d : 0x0e, o))
                return !vm.IsException();
            const ArrayBase& base = sint ? static_cast<Instances::fl_vec::Vector_int&>(o).GetArrayBase() :
                static_cast<Instances::fl_vec::Vector_uint&>(o).GetArrayBase();
            const UInt32 size = base.GetArraySize();
            AMF3WriteU29((size << 1) | 1);
            AMF3WriteByte(base.GetFixed() ? 1 : 0);
            for (UInt32 i = 0; i < size; ++i)
            {
                Value element;
                if (sint)
                    static_cast<Instances::fl_vec::Vector_int&>(o).Get(i, element);
                else
                    static_cast<Instances::fl_vec::Vector_uint&>(o).Get(i, element);
                AMF3WriteU32(sint ? static_cast<UInt32>(element.AsInt()) : element.AsUInt());
            }
            return !vm.IsException();
        }
        if (&tr == &vm.GetITraitsVectorNumber())
        {
            Instances::fl_vec::Vector_double& v = static_cast<Instances::fl_vec::Vector_double&>(o);
            if (AMF3WriteObjectRef(scope, 0x0f, o))
                return !vm.IsException();
            const UInt32 size = v.GetArrayBase().GetArraySize();
            AMF3WriteU29((size << 1) | 1);
            AMF3WriteByte(v.GetArrayBase().GetFixed() ? 1 : 0);
            for (UInt32 i = 0; i < size; ++i)
            {
                Value element;
                v.Get(i, element);
                AMF3WriteDouble(element.AsNumber());
            }
            return !vm.IsException();
        }
        if (&tr == &vm.GetITraitsVectorString())
        {
            // e753c0: the "String" type name, then string references without 0x06.
            Instances::fl_vec::Vector_String& v = static_cast<Instances::fl_vec::Vector_String&>(o);
            if (AMF3WriteObjectRef(scope, 0x10, o))
                return !vm.IsException();
            const UInt32 size = v.GetArrayBase().GetArraySize();
            AMF3WriteU29((size << 1) | 1);
            AMF3WriteByte(v.GetArrayBase().GetFixed() ? 1 : 0);
            AMF3WriteString(scope, vm.GetClassTraitsString().GetName());
            for (UInt32 i = 0; i < size; ++i)
            {
                Value element;
                v.Get(i, element);
                AMF3WriteString(scope, element.AsString());
            }
            return !vm.IsException();
        }
        return AMF3WriteGeneric(scope, o);
    }

    bool ByteArray::AMF3WriteValue(AMF3WriteScope& scope, const Value& v)
    {
        // e756a0
        VM& vm = GetVM();
        switch (v.GetKind())
        {
        case Value::kUndefined:
            AMF3WriteByte(0x00);
            break;
        case Value::kBoolean:
            AMF3WriteByte(v.AsBool() ? 0x03 : 0x02);
            break;
        case Value::kInt:
        case Value::kUInt:
            {
                // int and uint share the signed 29-bit test and conversion.
                const SInt32 i = v.GetKind() == Value::kInt ? v.AsInt() : static_cast<SInt32>(v.AsUInt());
                if (((i << 3) >> 3) == i)
                {
                    AMF3WriteByte(0x04);
                    AMF3WriteU29(static_cast<UInt32>(i) & 0x1fffffff);
                }
                else
                {
                    AMF3WriteByte(0x05);
                    AMF3WriteDouble(static_cast<Value::Number>(i));
                }
            }
            break;
        case Value::kNumber:
            AMF3WriteByte(0x05);
            AMF3WriteDouble(v.AsNumber());
            break;
        case Value::kString:
            AMF3WriteByte(0x06);
            AMF3WriteString(scope, v.AsString());
            break;
        case Value::kNamespace:
            // e75871: a namespace writes only its marker.
            AMF3WriteByte(v.GetNamespace() ? 0x0a : 0x01);
            break;
        case Value::kObject:
            if (!v.GetObject())
                AMF3WriteByte(0x01);
            else if (IsAMF3FunctionObject(v))
                AMF3WriteByte(0x00);
            else
                return AMF3WriteObject(scope, *v.GetObject());
            break;
        case Value::kThunk:
        case Value::kMethodInd:
        case Value::kVTableInd:
        case Value::kInstanceTraits:
        case Value::kClassTraits:
        case Value::kClass:
        case Value::kFunction:
            vm.ThrowArgumentError(VM::Error(VM::eInvalidArgumentError, vm SF_DEBUG_ARG("object")));
            return false;
        default:
            // Thunk functions and closures write nothing.
            break;
        }
        return !vm.IsException();
    }

    ///////////////////////////////////////////////////////////////////////
    bool ByteArray::AMF3ReadByte(UInt8& v)
    {
        return Read(&v, 1);
    }

    UInt32 ByteArray::AMF3ReadU29()
    {
        // e714e0: a short read returns the bits read so far.
        UInt8 b;
        if (!AMF3ReadByte(b))
            return 0;
        if (b < 0x80)
            return b;
        UInt32 v = UInt32(b & 0x7f) << 7;
        if (!AMF3ReadByte(b))
            return v;
        if (b < 0x80)
            return v | b;
        v = (v | (b & 0x7f)) << 7;
        if (!AMF3ReadByte(b))
            return v;
        if (b < 0x80)
            return v | b;
        v = (v | (b & 0x7f)) << 8;
        if (!AMF3ReadByte(b))
            return v;
        return v | b;
    }

    UInt32 ByteArray::AMF3ReadU32()
    {
        UInt8 bytes[4];
        if (!Read(bytes, 4))
            return 0;
        return (UInt32(bytes[0]) << 24) | (UInt32(bytes[1]) << 16) | (UInt32(bytes[2]) << 8) | bytes[3];
    }

    Value::Number ByteArray::AMF3ReadDouble()
    {
        // e71600: a short read is 0.0.
        UInt8 bytes[8];
        if (!Read(bytes, 8))
            return 0.0;
        UInt64 bits = 0;
        for (unsigned i = 0; i < 8; ++i)
            bits = (bits << 8) | bytes[i];
        Value::Number v;
        memcpy(&v, &bits, sizeof(v));
        return v;
    }

    ASString ByteArray::AMF3ReadString(AMF3ReadScope& scope)
    {
        // e71640: a bad reference is RangeError #1506 and reads as "".
        VM& vm = GetVM();
        ASString result = vm.GetStringManager().CreateEmptyString();
        const UInt32 u = AMF3ReadU29();
        if ((u & 1) == 0)
        {
            const UInt32 index = u >> 1;
            if (index >= scope.Strings.GetSize())
            {
                vm.ThrowRangeError(VM::Error(VM::eInvalidRangeError, vm));
                return result;
            }
            return ASString(scope.Strings[index].GetPtr());
        }
        const UInt32 len = u >> 1;
        if (len == 0)
            return result;
        // e1b270 reads the bytes and drops a leading UTF-8 BOM.
        if (!ReadUTFBytes(result, len))
            return result;
        scope.Strings.PushBack(Ptr<ASStringNode>(result.GetNode()));
        return result;
    }

    bool ByteArray::AMF3ReadObjectRef(AMF3ReadScope& scope, UInt32 u, Value& result)
    {
        // ea0b00
        VM& vm = GetVM();
        const UInt32 index = u >> 1;
        if (index >= scope.Objects.GetSize())
        {
            vm.ThrowRangeError(VM::Error(VM::eInvalidRangeError, vm));
            return false;
        }
        result = Value(scope.Objects[index].GetPtr());
        return true;
    }

    void ByteArray::AMF3ReadObject(AMF3ReadScope& scope, Value& result)
    {
        // e71780
        VM& vm = GetVM();
        const UInt32 u = AMF3ReadU29();
        if ((u & 1) == 0)
        {
            AMF3ReadObjectRef(scope, u, result);
            return;
        }
        const ClassTraits::Traits* externalizable = NULL;
        Class* cls = NULL;
        UPInt traitsIndex = 0;
        bool ext = false;
        bool dynamic = false;
        if ((u & 3) == 1)
        {
            traitsIndex = u >> 2;
            if (traitsIndex >= scope.Traits.GetSize())
            {
                vm.ThrowRangeError(VM::Error(VM::eInvalidRangeError, vm));
                return;
            }
            // A traits reference recomputes both flags from the class.
            InstanceTraits::Traits& itr = *scope.Traits[traitsIndex];
            cls = &itr.GetConstructor();
            externalizable = AMF3BuiltinTraits(vm, AS3::fl_utils::IExternalizableTI);
            ext = externalizable && externalizable->IsParentTypeOf(cls->GetClassTraits());
            dynamic = itr.IsDynamic();
        }
        else
        {
            ext = ((u >> 2) & 1) != 0;
            dynamic = ((u >> 3) & 1) != 0;
            const UInt32 sealed = u >> 4;
            // An empty or unregistered alias is an anonymous Object, silently.
            const ASString alias = AMF3ReadString(scope);
            if (!alias.IsEmpty())
                cls = vm.GetClassByAlias(alias);
            if (!cls)
                cls = &vm.GetClassObject();
            InstanceTraits::Traits& itr = cls->GetInstanceTraits();
            if (ext)
            {
                externalizable = AMF3BuiltinTraits(vm, AS3::fl_utils::IExternalizableTI);
                if (!externalizable || !externalizable->IsParentTypeOf(cls->GetClassTraits()))
                {
                    vm.ThrowArgumentError(VM::Error(VM::eReadExternalNotImplementedError, vm));
                    return;
                }
            }
            traitsIndex = scope.MemberStart.GetSize();
            scope.MemberStart.PushBack(scope.Members.GetSize());
            scope.MemberCount.PushBack(0);
            for (UInt32 k = 0; k < sealed; ++k)
            {
                // Only public slots match; any other name ends the read silently.
                const SlotInfo* si = itr.FindSlotInfo(AMF3ReadString(scope), vm.GetPublicNamespace());
                if (!si)
                    return;
                scope.Members.PushBack(si);
                ++scope.MemberCount[traitsIndex];
            }
            scope.Traits.PushBack(SPtr<InstanceTraits::Traits>(&itr));
        }
        if (vm.IsException())
            return;
        // e71b51: the AS3 constructor runs with no arguments.
        const UPInt depth = vm.GetCallStack().GetSize();
        Value created;
        cls->Construct(created, 0, NULL);
        if (vm.IsException())
            return;
        if (vm.GetCallStack().GetSize() > depth)
            vm.ExecuteCode(1);
        if (vm.IsException() || !created.IsObject() || !created.GetObject())
            return;
        result = created;
        AS3::Object& obj = *created.GetObject();
        scope.Objects.PushBack(SPtr<AS3::Object>(&obj));
        if (ext)
        {
            // e71bef: readExternal(this); nested reads share this context.
            const Multiname mn(vm.GetPublicNamespace(), vm.GetStringManager().CreateConstString("readExternal"));
            const Value arg(this);
            Value ignored;
            const bool saved = AMF3InReadExternal;
            AMF3InReadExternal = true;
            AS3::ExecutePropertyUnsafe(vm, mn, created, ignored, 1, &arg).DoNotCheck();
            AMF3InReadExternal = saved;
            return;
        }
        if (traitsIndex < scope.MemberStart.GetSize())
        {
            const UPInt start = scope.MemberStart[traitsIndex];
            const UPInt count = scope.MemberCount[traitsIndex];
            for (UPInt k = 0; k < count; ++k)
            {
                Value v;
                if (!AMF3ReadValue(scope, v))
                    return;
                if (!scope.Members[start + k]->SetSlotValue(vm, v, &obj))
                    return;
            }
        }
        if (!dynamic)
            return;
        // e71ea8: straight into the dynamic attributes, whatever the class.
        for (ASString name = AMF3ReadString(scope); !name.IsEmpty(); name = AMF3ReadString(scope))
        {
            Value v;
            if (!AMF3ReadValue(scope, v))
                return;
            obj.AddDynamicSlotValuePair(name, v);
        }
    }

    void ByteArray::AMF3ReadArray(AMF3ReadScope& scope, Value& result)
    {
        // e72080: associative keys become numeric indices (strtoul) and the
        // dense part, read afterwards, overwrites them.
        VM& vm = GetVM();
        const UInt32 u = AMF3ReadU29();
        if ((u & 1) == 0)
        {
            AMF3ReadObjectRef(scope, u, result);
            return;
        }
        const UInt32 len = u >> 1;
        SPtr<Instances::fl::Array> a = vm.MakeArray();
        scope.Objects.PushBack(SPtr<AS3::Object>(a.GetPtr()));
        for (ASString name = AMF3ReadString(scope); !name.IsEmpty(); name = AMF3ReadString(scope))
        {
            Value v;
            if (!AMF3ReadValue(scope, v))
                return;
            a->Set(strtoul(name.ToCStr(), NULL, 10), v);
        }
        for (UInt32 i = 0; i < len; ++i)
        {
            Value v;
            if (!AMF3ReadValue(scope, v))
                return;
            a->Set(i, v);
        }
        result = Value(a.GetPtr());
    }

    template <class T>
    static SPtr<T> MakeAMF3Instance(InstanceTraits::Traits& itr)
    {
        // Builtins get their C++ constructor only, as the 4.6.34 reader does.
        return InstanceTraits::MakeInstance(static_cast<typename T::TraitsType&>(itr));
    }

    void ByteArray::AMF3ReadVector(AMF3ReadScope& scope, UInt8 marker, Value& result)
    {
        // e72990/e72ad0/e72c10/e72d40: elements are stored without coercion.
        VM& vm = GetVM();
        const UInt32 u = AMF3ReadU29();
        if ((u & 1) == 0)
        {
            AMF3ReadObjectRef(scope, u, result);
            return;
        }
        const UInt32 len = u >> 1;
        UInt8 fixed;
        if (!AMF3ReadByte(fixed))
        {
            if (marker != 0x10)
                result.SetNull();
            return;
        }
        switch (marker)
        {
        case 0x0d:
        case 0x0e:
            if (marker == 0x0d)
            {
                SPtr<Instances::fl_vec::Vector_int> v =
                    MakeAMF3Instance<Instances::fl_vec::Vector_int>(vm.GetITraitsVectorSInt());
                scope.Objects.PushBack(SPtr<AS3::Object>(v.GetPtr()));
                result = Value(v.GetPtr());
                for (UInt32 i = 0; i < len; ++i)
                    v->PushBack(static_cast<SInt32>(AMF3ReadU32()));
                const_cast<ArrayBase&>(v->GetArrayBase()).SetFixed(fixed != 0);
            }
            else
            {
                SPtr<Instances::fl_vec::Vector_uint> v =
                    MakeAMF3Instance<Instances::fl_vec::Vector_uint>(vm.GetITraitsVectorUInt());
                scope.Objects.PushBack(SPtr<AS3::Object>(v.GetPtr()));
                result = Value(v.GetPtr());
                for (UInt32 i = 0; i < len; ++i)
                    v->PushBack(AMF3ReadU32());
                const_cast<ArrayBase&>(v->GetArrayBase()).SetFixed(fixed != 0);
            }
            return;
        case 0x0f:
            {
                SPtr<Instances::fl_vec::Vector_double> v =
                    MakeAMF3Instance<Instances::fl_vec::Vector_double>(vm.GetITraitsVectorNumber());
                scope.Objects.PushBack(SPtr<AS3::Object>(v.GetPtr()));
                result = Value(v.GetPtr());
                for (UInt32 i = 0; i < len; ++i)
                    v->PushBack(AMF3ReadDouble());
                const_cast<ArrayBase&>(v->GetArrayBase()).SetFixed(fixed != 0);
            }
            return;
        default:
            break;
        }
        // e72d40: "String" is Vector.<String> of bare string references; any
        // other name is an alias, and an unknown one is Object.
        const ASString type = AMF3ReadString(scope);
        if (strcmp(type.ToCStr(), "String") == 0)
        {
            SPtr<Instances::fl_vec::Vector_String> v =
                MakeAMF3Instance<Instances::fl_vec::Vector_String>(vm.GetITraitsVectorString());
            scope.Objects.PushBack(SPtr<AS3::Object>(v.GetPtr()));
            result = Value(v.GetPtr());
            for (UInt32 i = 0; i < len; ++i)
                v->PushBack(AMF3ReadString(scope).GetNode());
            const_cast<ArrayBase&>(v->GetArrayBase()).SetFixed(fixed != 0);
            return;
        }
        Class* elementClass = type.IsEmpty() ? NULL : vm.GetClassByAlias(type);
        if (!elementClass)
            elementClass = &vm.GetClassObject();
        const Value typeArg(elementClass);
        Class& vectorClass = vm.GetClassVector().ApplyTypeArgs(1, &typeArg);
        if (vm.IsException())
            return;
        SPtr<Instances::fl_vec::Vector_object> v =
            MakeAMF3Instance<Instances::fl_vec::Vector_object>(vectorClass.GetInstanceTraits());
        scope.Objects.PushBack(SPtr<AS3::Object>(v.GetPtr()));
        result = Value(v.GetPtr());
        for (UInt32 i = 0; i < len; ++i)
        {
            Value element;
            if (!AMF3ReadValue(scope, element))
                break;
            v->PushBack(element);
        }
        const_cast<ArrayBase&>(v->GetArrayBase()).SetFixed(fixed != 0);
    }

    void ByteArray::AMF3ReadDictionary(AMF3ReadScope& scope, Value& result)
    {
        // e72550
        VM& vm = GetVM();
        const UInt32 u = AMF3ReadU29();
        if ((u & 1) == 0)
        {
            AMF3ReadObjectRef(scope, u, result);
            return;
        }
        const UInt32 count = u >> 1;
        UInt8 weak;
        if (!AMF3ReadByte(weak))
            return;
        const ClassTraits::Traits* ctr = AMF3BuiltinTraits(vm, AS3::fl_utils::DictionaryTI);
        if (!ctr)
            return;
        SPtr<Instances::fl_utils::Dictionary> d =
            MakeAMF3Instance<Instances::fl_utils::Dictionary>(ctr->GetInstanceTraits());
        d->WeakKeys = weak != 0;
        scope.Objects.PushBack(SPtr<AS3::Object>(d.GetPtr()));
        for (UInt32 i = 0; i < count; ++i)
        {
            Value key, value;
            if (!AMF3ReadValue(scope, key) || !AMF3ReadValue(scope, value))
                return;
            d->AddDynamicSlotValuePair(key, value);
        }
        result = Value(d.GetPtr());
    }

    bool ByteArray::AMF3ReadValue(AMF3ReadScope& scope, Value& result)
    {
        // e730a0: success is "no exception pending"; 0x07 and markers above
        // 0x11 read nothing and leave the result unchanged.
        VM& vm = GetVM();
        UInt8 marker;
        if (!AMF3ReadByte(marker))
            return false;
        switch (marker)
        {
        case 0x00:
            result.SetUndefined();
            break;
        case 0x01:
            result.SetNull();
            break;
        case 0x02:
        case 0x03:
            result.SetBool(marker == 0x03);
            break;
        case 0x04:
            result.SetSInt32(static_cast<SInt32>(AMF3ReadU29() << 3) >> 3);
            break;
        case 0x05:
            result.SetNumber(AMF3ReadDouble());
            break;
        case 0x06:
            result = AMF3ReadString(scope);
            break;
        case 0x08:
            {
                // e72400: the reference flag's other bits are ignored.
                const UInt32 u = AMF3ReadU29();
                if ((u & 1) == 0)
                {
                    AMF3ReadObjectRef(scope, u, result);
                    break;
                }
                const ClassTraits::Traits* ctr = AMF3BuiltinTraits(vm, AS3::fl::DateTI);
                if (!ctr)
                    break;
                SPtr<Instances::fl::Date> date = MakeAMF3Instance<Instances::fl::Date>(ctr->GetInstanceTraits());
                scope.Objects.PushBack(SPtr<AS3::Object>(date.GetPtr()));
                date->TimeValue = AMF3ReadDouble();
                result = Value(date.GetPtr());
            }
            break;
        case 0x09:
            AMF3ReadArray(scope, result);
            break;
        case 0x0a:
            AMF3ReadObject(scope, result);
            break;
        case 0x0b:
            {
                // e731e4: empty or unreadable text is null and takes no reference.
                const UInt32 u = AMF3ReadU29();
                if ((u & 1) == 0)
                {
                    AMF3ReadObjectRef(scope, u, result);
                    break;
                }
                const UInt32 len = u >> 1;
                if (len == 0 || !CanRead(len))
                {
                    if (len != 0)
                        ThrowEOFError();
                    result.SetNull();
                    break;
                }
                const Value text(vm.GetStringManager().CreateString(
                    reinterpret_cast<const char*>(Data.GetDataPtr() + Position), len));
                Position += len;
                Value xml;
                if (!vm.ConstructBuiltinValue(xml, "XML", 1, &text))
                    break;
                scope.Objects.PushBack(SPtr<AS3::Object>(xml.GetObject()));
                result = xml;
            }
            break;
        case 0x0c:
            {
                // e722b0
                const UInt32 u = AMF3ReadU29();
                if ((u & 1) == 0)
                {
                    AMF3ReadObjectRef(scope, u, result);
                    break;
                }
                const UInt32 len = u >> 1;
                const ClassTraits::Traits* ctr = AMF3BuiltinTraits(vm, AS3::fl_utils::ByteArrayTI);
                if (!ctr)
                    break;
                SPtr<ByteArray> bytes = MakeAMF3Instance<ByteArray>(ctr->GetInstanceTraits());
                scope.Objects.PushBack(SPtr<AS3::Object>(bytes.GetPtr()));
                result = Value(bytes.GetPtr());
                bytes->Resize(len);
                bytes->Position = 0;
                Read(bytes->Data.GetDataPtr(), len).DoNotCheck();
            }
            break;
        case 0x0d:
        case 0x0e:
        case 0x0f:
        case 0x10:
            AMF3ReadVector(scope, marker, result);
            break;
        case 0x11:
            AMF3ReadDictionary(scope, result);
            break;
        default:
            break;
        }
        return !vm.IsException();
    }
