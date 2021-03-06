//
//  MutableDictionary.c
//  CObjects
//
//  Created by George Boumis on 15/2/13.
//  Copyright (c) 2013 George Boumis. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>

#include <cobj.h>
#include <MutableDictionary.r>

#ifndef __MUTABLE_DICTIONARY_LEVEL1_INITIAL_SIZE
#if __LP64__
#define __MUTABLE_DICTIONARY_LEVEL1_INITIAL_SIZE 2L
#else
#define __MUTABLE_DICTIONARY_LEVEL1_INITIAL_SIZE 2
#endif
#endif /* __MUTABLE_DICTIONARY_LEVEL1_INITIAL_SIZE */

#ifndef __MUTABLE_DICTIONARY_DEFAULT_LOAD_FACTOR
#define __MUTABLE_DICTIONARY_DEFAULT_LOAD_FACTOR 0.75
#endif /* __MUTABLE_DICTIONARY_DEFAULT_LOAD_FACTOR */


const void * MutableDictionary = NULL;
const void * MutableDictionaryClass = NULL;

static int __allocateLevel1(struct MutableDictionary *const self, UInteger size);
//static void __setObjectForKey(struct MutableDictionary *const self, void *const object, const void *const key);
static int __rebuild(struct MutableDictionary *const self);
static int __addCoupleWithHash(struct MutableDictionary *const self, UInteger hash, CoupleRef couple);
static void __emptyLevel1(struct MutableDictionary *const self);
static CoupleRef __findCoupleInArrayForKey(const struct MutableDictionary *const self, VectorRef array, const void *const key);
inline static UInteger __hash(UInteger h) {
#define triple_shift(n,s) ((n) >> (s))
	// This function ensures that hashCodes that differ only by
	// constant multiples at each bit position have a bounded
	// number of collisions (approximately 8 at default load factor).
	// h ^= (h  > > > 20) ^ (h  > > > 12);
	// return h ^ (h  > > > 7) ^ (h  > > > 4);
	h ^=  triple_shift(h, 20) ^ triple_shift(h, 12);
	return h ^  triple_shift(h, 7) ^ triple_shift(h, 4);
#undef triple_shift
}

//inline static UInteger __deduceSize(UInteger count, float loadFactor) {
//	UInteger size = 0;
//	UInteger potentialSize = (UInteger) ((float)count / loadFactor);
//	UInteger basicSize = potentialSize / __MUTABLE_DICTIONARY_LEVEL1_INITIAL_SIZE;
//	size = basicSize * 10;
//	if ( (potentialSize % __MUTABLE_DICTIONARY_LEVEL1_INITIAL_SIZE) != 0 ) size *= 2;
//	return ((size <= 10) ? 10 : size);
//}

/* Methods */

static void * MutableDictionary_constructor (void * _self, va_list * app) {
	struct MutableDictionary *self = super_constructor(Dictionary, _self, app);
	
	int error;
	
	self->loadFactor = __MUTABLE_DICTIONARY_DEFAULT_LOAD_FACTOR;
	self->couples = new(Vector, NULL);
	if ( self->couples == NULL ) return free(self->level1), free(self), NULL;
	
	/* fill the structure */
	{
		ObjectRef key = NULL, value = NULL;
		while ( (key = va_arg(*app, void *)) != NULL ) {
			value = va_arg(*app, void *);
			if (value == NULL) return release(self->couples), free(self), errno = EINVAL, NULL;
			CoupleRef couple = new(Couple, key, value, NULL);
//			insertObject(self->couples, couple), release(couple);
			addObject(self->couples, couple), release(couple);
		}
	}
	UInteger itemsCount = getCollectionCount(self->couples);
	if (itemsCount<=0) {
		if ( __allocateLevel1(self, __MUTABLE_DICTIONARY_LEVEL1_INITIAL_SIZE) != 0 ) return error = errno, release(self->couples), free(self),  errno = error, NULL;
		self->size = __MUTABLE_DICTIONARY_LEVEL1_INITIAL_SIZE;
		return self;
	}
	
	/* Deduce size */
//	UInteger size = __deduceSize(itemsCount, self->loadFactor);
	UInteger size = __MUTABLE_DICTIONARY_LEVEL1_INITIAL_SIZE;
	while ( (UInteger)(size * self->loadFactor) < itemsCount ) size *= 2;
	self->size = size;
	if ( __allocateLevel1(self, size) != 0 ) return error = errno, release(self->couples), free(self),  errno = error, NULL;
	
	if ( __rebuild(self) != 0 ) return error = errno, __emptyLevel1(self), release(self->couples), free(self),  errno = error, NULL;
	return self;
}

static void * MutableDictionary_destructor (void * _self) {
	struct MutableDictionary *self = super_destructor(Dictionary, _self);
	__emptyLevel1(self);
	release(self->couples), self->couples = NULL;
	return self;
}

static void * MutableDictionaryClass_constructor (void * _self, va_list *app) {
	struct MutableDictionaryClass * self = super_constructor(MutableDictionaryClass, _self, app);
	typedef void (*voidf) ();
	voidf selector;
	va_list ap;
	va_copy(ap, *app);
	while ( (selector = va_arg(ap, voidf)) ) {
		voidf method = va_arg(ap, voidf);
		if (selector == (voidf) setObjectForKey )
			* (voidf *) & self->setObjectForKey = method;
		else if (selector == (voidf) setMutableDictionaryLoadFactor )
			* (voidf *) & self->setMutableDictionaryLoadFactor = method;
		else if (selector == (voidf) removeObjectForKey )
			* (voidf *) & self->removeObjectForKey = method;
	}
	va_end(ap);
	return self;
}



static int __allocateLevel1(struct MutableDictionary *const self, UInteger size) {
	if (self->level1 != NULL)
		__emptyLevel1(self);
	self->level1 = calloc(size, sizeof(void *));
	if (self->level1 == NULL)
		return -1;
	return 0;
}

static void __emptyLevel1(struct MutableDictionary *const self) {
	UInteger count = self->size;
	for (UInteger i=0; i<count; i++) {
		VectorRef collisionList = self->level1[i];
		if (collisionList) release(collisionList), self->level1[i] = NULL;
	}
	free(self->level1), self->level1 = NULL;
}

//static void __setObjectForKey(struct MutableDictionary *const self, void *const object, const void *const key) {
//	CoupleRef couple = __findCoupleForKey(self, key);
//	setValue(couple, object);
//}

static CoupleRef __findCoupleInArrayForKey(const struct MutableDictionary *const self, VectorRef array, const void *const key) {
	UInteger count = getCollectionCount(array);
	CoupleRef couple = NULL;
	for (UInteger index = 0; index<count && (couple = getObjectAtIndex(array, index)) && ! equals(getKey(couple), key); index++, couple=NULL);
	if (couple != NULL)
		return couple;
	return NULL;
}


static int __addCoupleWithHash(struct MutableDictionary *const self, UInteger hash, CoupleRef couple) {
	UInteger index = __hash(hash) % self->size;
	VectorRef collisionList = self->level1[index];
	if ( collisionList == NULL) {
//		collisionList = new(MutableArray, NULL);
		collisionList = new(Vector, 5, 5, NULL);
		if (collisionList == NULL)/* If allocation failed, bail out */
			return -1;
		self->level1[index] = collisionList;
//		insertObject(collisionList, couple), ((struct Dictionary *)self)->count++;
		addObject(collisionList, couple), ((struct Dictionary *)self)->count++;
	}
	else {
		ObjectRef key = getKey(couple);
		ObjectRef value = getValue(couple);
		CoupleRef existantCouple = __findCoupleInArrayForKey(self, collisionList, key);
		if ( existantCouple == NULL ) /* Not present */
//			insertObject(collisionList, couple), ((struct Dictionary *)self)->count++;
			addObject(collisionList, couple), ((struct Dictionary *)self)->count++;
		else /* Already present */
			setValue( existantCouple, value);
	}

	return 0;
}

static int __rebuild(struct MutableDictionary *const self) {
	((struct Dictionary *)self)->count = 0;
	UInteger count = getCollectionCount(self->couples);
	CoupleRef couple = NULL;
	for (UInteger index = 0; index < count && (couple = getObjectAtIndex(self->couples, index)); index++) {
		ObjectRef key = getKey(couple);
		UInteger h = hash(key);
		if ( __addCoupleWithHash(self, h, couple) != 0 ) return -1;
//		((struct Dictionary *)self)->count++;
	}
	return 0;
}

static void MutableDictionary_setObjectForKey(void *const _self, void *const object, void *const key) {
	struct MutableDictionary *const self = _self;
	CoupleRef couple = new(Couple, key, object, NULL);;
//	UInteger newSize = __deduceSize(((struct Dictionary *)self)->count+1, self->loadFactor);
	if ( (UInteger)(self->size * self->loadFactor) < ((struct Dictionary *)self)->count+1 ) {
		
		__allocateLevel1(self, self->size * 2);
		self->size *= 2;
		__rebuild(self);
	}
//	insertObject(self->couples, couple);
	addObject(self->couples, couple);
	UInteger h = hash(key);
	__addCoupleWithHash(self, h, couple);
	release(couple);
}

static ObjectRef MutableDictionary_objectForKey(const void *const _self, void *const key) {
	const struct MutableDictionary *const self = _self;
	UInteger h = hash(key);
	UInteger index = __hash(h) % self->size;
	VectorRef collisionList = self->level1[index];
	if ( collisionList == NULL ) return NULL;
	
	CoupleRef couple = __findCoupleInArrayForKey(self, collisionList, key);
	ObjectRef value = NULL;
	if (couple)
		value = getValue(couple);
	return value;
}

static void MutableDictionary_setMutableDictionaryLoadFactor(void *const _self, float loadFactor) {
	struct MutableDictionary *const self = _self;
	if ( loadFactor < 0 || loadFactor > 1.0) return;
	if ( fabsf(loadFactor - self->loadFactor) < 0.01 ) return;
	float oldFactor = self->loadFactor;
	self->loadFactor = loadFactor;
	if ( loadFactor < oldFactor )
		__allocateLevel1(self, self->size), __rebuild(self);
	self->loadFactor = loadFactor;
}

static void MutableDictionary_removeObjectForKey(void *const _self, void *const key) {
	struct MutableDictionary *const self = _self;
	UInteger h = hash(key);
	UInteger index = __hash(h) % self->size;
	VectorRef collisionList = self->level1[index];
	if ( collisionList == NULL ) return;
	
	
	CoupleRef couple = __findCoupleInArrayForKey(self, collisionList, key);
	if ( couple == NULL ) return;
	
	removeObject(self->couples, couple);
	removeObject(collisionList, couple);
}

void initMutableDictionary() {
	initCouple();
	initDictionary();
	initArray();
	initMutableArray();
	initVector();
	
	if ( ! MutableDictionaryClass) {
		MutableDictionaryClass = new(DictionaryClass, "MutableDictionaryClass", DictionaryClass, sizeof(struct MutableDictionaryClass),
							  constructor, MutableDictionaryClass_constructor, NULL);
	}
	if ( ! MutableDictionary ) {
		MutableDictionary = new(MutableDictionaryClass, "MutableDictionary", Dictionary, sizeof(struct MutableDictionary),
								constructor, MutableDictionary_constructor,
								destructor, MutableDictionary_destructor,
								/* overrides */
								objectForKey, MutableDictionary_objectForKey,
								
								/* new */
								setObjectForKey, MutableDictionary_setObjectForKey,
								setMutableDictionaryLoadFactor, MutableDictionary_setMutableDictionaryLoadFactor,
								removeObjectForKey, MutableDictionary_removeObjectForKey,
								NULL);
	}
}

void deallocMutableDictionary() {
//	free((void *)MutableDictionary), MutableDictionary = NULL;
//	free((void *)MutableDictionaryClass), MutableDictionaryClass = NULL;;

	release((void *)MutableDictionary), MutableDictionary = NULL;
	release((void *)MutableDictionaryClass), MutableDictionaryClass = NULL;
	deallocVector();
	deallocArray();
	deallocMutableArray();
	deallocCouple();
	deallocDictionary();
}

void setObjectForKey(void *const self, void *const object, void *const key) {
	COAssertNoNullOrBailOut(self,EINVAL);
	COAssertNoNullOrBailOut(object,EINVAL);
	COAssertNoNullOrBailOut(key,EINVAL);
	const struct MutableDictionaryClass *class = classOf(self);
	COAssertNoNullOrBailOut(class,EINVAL);
	COAssertNoNullOrBailOut(class->setObjectForKey,EINVAL);
	class->setObjectForKey(self, object, key);
}

void setMutableDictionaryLoadFactor(void *const self, float loadFactor) {
	COAssertNoNullOrBailOut(self,EINVAL);
	const struct MutableDictionaryClass *class = classOf(self);
	COAssertNoNullOrBailOut(class,EINVAL);
	COAssertNoNullOrBailOut(class->setMutableDictionaryLoadFactor,EINVAL);
	class->setMutableDictionaryLoadFactor(self, loadFactor);
}

void removeObjectForKey(void *const self, void *const key) {
	COAssertNoNullOrBailOut(self,EINVAL);
	COAssertNoNullOrBailOut(key,EINVAL);
	
	const struct MutableDictionaryClass *class = classOf(self);
	COAssertNoNullOrBailOut(class,EINVAL);
	COAssertNoNullOrBailOut(class->removeObjectForKey,EINVAL);
	class->removeObjectForKey(self, key);
}

#ifdef __PROFILING__
void MutableDictionaryPrintfStatistics(const void *const _self) {
	assert( _self != NULL );
	const struct MutableDictionary *self = _self;
	UInteger empty = 0, minCollisionListSize = INT_MAX, maxCollisionListSize = 0, medianCollisionListSize = 0;
	UInteger count = ((struct Dictionary *)self)->count;

	for (UInteger i=0; i<self->size; i++) {
		VectorRef cl = self->level1[i];
		if ( cl == NULL ) { empty++; continue; }
		UInteger count = getCollectionCount(cl);
		medianCollisionListSize += count;
		if ( count < minCollisionListSize ) minCollisionListSize = count;
		else if ( count > maxCollisionListSize ) maxCollisionListSize = count;
	}
	
	printf("MutableDictionary Statistics{ size:[%lu], count:[%lu], empty:[%lu], filled:[%lu] minCollisionListSize:[%lu] maxCollisionListSize:[%lu] medianCollisionListSize:[%lu]}\n", self->size, count, empty, self->size - empty, minCollisionListSize, maxCollisionListSize, medianCollisionListSize/count);
}
#endif

