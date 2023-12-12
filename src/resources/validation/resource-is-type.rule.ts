import { Inject } from '@nestjs/common';
import {
    registerDecorator,
    ValidationOptions,
    ValidatorConstraint,
    ValidatorConstraintInterface,
    ValidationArguments,
} from 'class-validator';
import { ResourceType } from 'src/entities/AbstractStorageResource';
import { ResourceRepository } from '../resource.repository';

@ValidatorConstraint({ async: true })
export class ResourceIsTypeConstraint implements ValidatorConstraintInterface {
    constructor(@Inject('RESOURCE_REPOSITORY') private readonly repo: ResourceRepository) {}

    validate(resourceId: string, args: ValidationArguments) {
        const [ResType] = args.constraints;
        const resource = this.repo.find(resourceId);
        if (!resource) return false;

        for (const constraintType of ResType) {
            if (constraintType == resource.type) return true;
        }
    }

    defaultMessage(args: ValidationArguments) {
        return `${args.value}: Type of this resource must be ${args.constraints}.`;
    }
}

export function ResourceTypeIsIn(property: ResourceType[], validationOptions?: ValidationOptions) {
    return function (object: object, propertyName: string) {
        registerDecorator({
            target: object.constructor,
            propertyName: propertyName,
            options: validationOptions,
            constraints: [property],
            validator: ResourceIsTypeConstraint,
        });
    };
}
