import { Inject } from '@nestjs/common';
import {
    registerDecorator,
    ValidationArguments,
    ValidationOptions,
    ValidatorConstraint,
    ValidatorConstraintInterface,
} from 'class-validator';
import { ResourceRepository } from '../resource.repository';
import { StorageResource } from 'src/entities/StorageResource';
import { ResourceType } from 'src/entities/AbstractStorageResource';
import { Export } from '../../entities/Export';

@ValidatorConstraint({ async: true })
export class ResourceReadyConstraint implements ValidatorConstraintInterface {
    constructor(@Inject('RESOURCE_REPOSITORY') private readonly repo: ResourceRepository) {}

    // eslint-disable-next-line @typescript-eslint/no-unused-vars
    validate(resourceId: string, args: ValidationArguments) {
        const resource: StorageResource = this.repo.find(resourceId);
        if (!resource) return false;
        if (resource.state && resource.locked) return false;
        const exportExist = this.repo.resources.find((r: Export) => {
            if (!r.params) return;
            return r.type == ResourceType.Export && r.params.relationId == resourceId;
        });
        return !exportExist;
    }

    defaultMessage(args: ValidationArguments) {
        // here you can provide default error message if validation failed
        return `Resource ${args.value} is busy.`;
    }
}

export function ResourceReady(validationOptions?: ValidationOptions) {
    return function (object: object, propertyName: string) {
        registerDecorator({
            target: object.constructor,
            propertyName: propertyName,
            options: validationOptions,
            constraints: [],
            validator: ResourceReadyConstraint,
        });
    };
}
